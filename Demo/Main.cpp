#include <MakeASound/MakeASound.h>
#include <eacp/WebView/WebView.h>
#include <eacp/Core/Threads/Timer.h>
#include <WebResources.h>

#include <atomic>
#include <random>
#include <string>
#include <vector>

namespace
{
float getRandomFloat()
{
    static std::default_random_engine engine {std::random_device {}()};
    static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    return dist(engine);
}

struct AudioState
{
    std::atomic<bool> playing {false};
    std::atomic<float> gain {0.1f};
};

void renderWhiteNoise(MakeASound::AudioCallbackInfo& info, AudioState& state)
{
    auto playing = state.playing.load(std::memory_order_relaxed);
    auto gain = state.gain.load(std::memory_order_relaxed);

    for (auto channel = 0; channel < info.numOutputs; ++channel)
    {
        auto channelData = info.getOutput<float>(channel);

        if (!playing)
        {
            std::fill_n(channelData, info.numSamples, 0.0f);
            continue;
        }

        for (auto sample = 0; sample < info.numSamples; ++sample)
            channelData[sample] = getRandomFloat() * gain;
    }
}

struct DropdownItem
{
    MIRO_REFLECT(id, label)

    int id {};
    std::string label;
};

struct DropdownInfo
{
    MIRO_REFLECT(items, currentId)

    MakeASound::Vector<DropdownItem> items;
    int currentId {};
};

struct AudioControls
{
    MIRO_REFLECT(playing, gain)

    bool playing {};
    double gain {};
};

struct UIState
{
    MIRO_REFLECT(playing, gain, blockSize, devices, sampleRates, midiPorts)

    bool playing {};
    double gain {};
    int blockSize {};
    DropdownInfo devices;
    DropdownInfo sampleRates;
    DropdownInfo midiPorts;
};

DropdownInfo buildOutputDevices(MakeASound::DeviceManager& manager, int currentId)
{
    auto info = DropdownInfo {};
    info.currentId = currentId;

    for (auto& device: manager.getDevices())
    {
        if (device.outputChannels == 0)
            continue;

        info.items.create(device.id, device.name);
    }

    return info;
}

DropdownInfo buildSampleRates(const MakeASound::DeviceInfo& device, int currentRate)
{
    auto info = DropdownInfo {};
    info.currentId = currentRate;

    for (auto rate: device.sampleRates)
        info.items.create(rate, std::to_string(rate) + " Hz");

    return info;
}

DropdownInfo buildMidiPorts(
    const MakeASound::Vector<MakeASound::MidiPortInfo>& ports,
    int currentPortId)
{
    auto info = DropdownInfo {};
    info.currentId = currentPortId;
    info.items.create(-1, std::string {"(none)"});

    for (auto& port: ports)
        info.items.create(port.id, port.name);

    return info;
}
} // namespace

struct DemoApp
{
    DemoApp()
    {
        config = manager.getDefaultConfig();
        manager.start(config, [this](auto& info) { renderWhiteNoise(info, audio); });

        webView.addScriptMessageHandler(
            "demo", [this](const std::string& msg) { handleMessage(msg); });

        window.setContentView(webView);
    }

    void handleMessage(const std::string& jsonStr)
    {
        auto json = Miro::Json::parse(jsonStr);

        if (!json.isObject())
            return;

        auto& obj = json.asObject();
        auto* type = Miro::Json::find(obj, "type");

        if (type == nullptr || !type->isString())
            return;

        auto kind = type->asString();

        if (kind == "ready")
            sendState();
        else if (kind == "playing")
            audio.playing.store(Miro::Json::find(obj, "value")->asBool());
        else if (kind == "gain")
            audio.gain.store(
                static_cast<float>(Miro::Json::find(obj, "value")->asNumber()));
        else if (kind == "sampleRate")
            applySampleRate(
                static_cast<int>(Miro::Json::find(obj, "value")->asNumber()));
        else if (kind == "blockSize")
            applyBlockSize(
                static_cast<int>(Miro::Json::find(obj, "value")->asNumber()));
        else if (kind == "device")
            applyDevice(static_cast<int>(Miro::Json::find(obj, "id")->asNumber()));
        else if (kind == "midiPort")
            applyMidiPort(static_cast<int>(Miro::Json::find(obj, "id")->asNumber()));
    }

    void applySampleRate(int rate)
    {
        config.sampleRate = rate;
        manager.setConfig(config);
    }

    void applyBlockSize(int size)
    {
        config.maxBlockSize = size;
        manager.setConfig(config);
    }

    void applyMidiPort(int portId)
    {
        midi.closeAllInputs();

        if (portId < 0)
        {
            currentMidiPortId = -1;
            return;
        }

        midi.openInput(portId,
                       [this](const MakeASound::MidiMessage& msg)
                       { handleIncomingMidi(msg); });
        currentMidiPortId = portId;
    }

    void handleIncomingMidi(const MakeASound::MidiMessage& msg)
    {
        if (msg.bytes.size() >= 3)
        {
            auto status = msg.bytes[0] & 0xF0;
            auto data1 = msg.bytes[1];
            auto data2 = msg.bytes[2];

            if (status == 0x90 && data2 > 0)
            {
                audio.playing.store(true);
                audio.gain.store(static_cast<float>(data2) / 127.0f);
            }
            else if (status == 0x80 || (status == 0x90 && data2 == 0))
            {
                audio.playing.store(false);
            }
            else if (status == 0xB0 && data1 == 7)
            {
                audio.gain.store(static_cast<float>(data2) / 127.0f);
            }
        }

        eacp::Threads::callAsync([this, msg]() { publishMidiToJS(msg); });
    }

    void publishMidiToJS(const MakeASound::MidiMessage& msg)
    {
        auto text = MakeASound::formatMessage(msg);
        webView.evaluateJavaScript("window.demoMidiEvent("
                                   + Miro::toJSONString(text) + ");");

        auto controls = AudioControls {audio.playing.load(),
                                       static_cast<double>(audio.gain.load())};
        webView.evaluateJavaScript("window.demoUpdateAudio("
                                   + Miro::toJSONString(controls) + ");");
    }

    void applyDevice(int deviceId)
    {
        for (auto& device: manager.getDevices())
        {
            if (device.id != deviceId || device.outputChannels == 0)
                continue;

            config.output = MakeASound::StreamParameters {device, false};

            if (!device.sampleRates.empty()
                && std::ranges::find(device.sampleRates, config.sampleRate)
                       == device.sampleRates.end())
                config.sampleRate = device.sampleRates.front();

            manager.setConfig(config);
            sendState();
            return;
        }
    }

    void sendState()
    {
        auto state = UIState {};
        state.playing = audio.playing.load();
        state.gain = static_cast<double>(audio.gain.load());
        state.blockSize = config.maxBlockSize;

        auto currentDeviceId = config.output ? config.output->device.id : 0;
        state.devices = buildOutputDevices(manager, currentDeviceId);

        if (config.output)
            state.sampleRates =
                buildSampleRates(config.output->device, config.sampleRate);

        lastInputPorts = midi.getInputPorts();
        state.midiPorts = buildMidiPorts(lastInputPorts, currentMidiPortId);

        webView.evaluateJavaScript("window.demoSetState(" + Miro::toJSONString(state)
                                   + ");");
    }

    void pollMidiPorts()
    {
        auto current = midi.getInputPorts();

        if (current == lastInputPorts)
            return;

        lastInputPorts = std::move(current);

        auto info = buildMidiPorts(lastInputPorts, currentMidiPortId);
        webView.evaluateJavaScript("window.demoSetMidiPorts("
                                   + Miro::toJSONString(info) + ");");
    }

    AudioState audio;
    MakeASound::DeviceManager manager;
    MakeASound::MidiManager midi;
    MakeASound::StreamConfig config;
    int currentMidiPortId {-1};
    MakeASound::Vector<MakeASound::MidiPortInfo> lastInputPorts;
    eacp::Graphics::WebView webView {eacp::Graphics::embeddedOptions("DemoWeb")};
    eacp::Graphics::Window window;
    eacp::Threads::Timer midiPollTimer {[this] { pollMidiPorts(); }, 2};
};

int main()
{
    eacp::Apps::run<DemoApp>();
    return 0;
}
