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

    for (auto channel = 0u; channel < info.numOutputs; ++channel)
    {
        auto channelData = info.getOutput<float>(channel);

        if (!playing)
        {
            std::fill_n(channelData, info.numSamples, 0.0f);
            continue;
        }

        for (auto sample = 0u; sample < info.numSamples; ++sample)
            channelData[sample] = getRandomFloat() * gain;
    }
}

struct DeviceOption
{
    MIRO_REFLECT(id, name, sampleRates)

    unsigned int id {};
    std::string name;
    std::vector<unsigned int> sampleRates;
};

struct AudioControls
{
    MIRO_REFLECT(playing, gain)

    bool playing {};
    double gain {};
};

struct MidiPortsState
{
    MIRO_REFLECT(midiInputPorts, currentMidiPortId)

    std::vector<MakeASound::MidiPortInfo> midiInputPorts;
    int currentMidiPortId {-1};
};

struct UIState
{
    MIRO_REFLECT(playing,
                 gain,
                 currentDeviceId,
                 sampleRate,
                 blockSize,
                 outputDevices,
                 midiInputPorts,
                 currentMidiPortId)

    bool playing {};
    double gain {};
    unsigned int currentDeviceId {};
    unsigned int sampleRate {};
    unsigned int blockSize {};
    std::vector<DeviceOption> outputDevices;
    std::vector<MakeASound::MidiPortInfo> midiInputPorts;
    int currentMidiPortId {-1};
};
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
            applySampleRate(static_cast<unsigned int>(
                Miro::Json::find(obj, "value")->asNumber()));
        else if (kind == "blockSize")
            applyBlockSize(static_cast<unsigned int>(
                Miro::Json::find(obj, "value")->asNumber()));
        else if (kind == "device")
            applyDevice(
                static_cast<unsigned int>(Miro::Json::find(obj, "id")->asNumber()));
        else if (kind == "midiPort")
            applyMidiPort(
                static_cast<int>(Miro::Json::find(obj, "id")->asNumber()));
    }

    void applySampleRate(unsigned int rate)
    {
        config.sampleRate = rate;
        manager.setConfig(config);
    }

    void applyBlockSize(unsigned int size)
    {
        config.maxBlockSize = size;
        manager.setConfig(config);
    }

    void applyMidiPort(int portId)
    {
        if (portId < 0)
        {
            midi.closeInput();
            currentMidiPortId = -1;
            return;
        }

        midi.openInput(static_cast<unsigned int>(portId),
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
        webView.evaluateJavaScript("window.demoMidiEvent("
                                   + Miro::toJSONString(msg) + ");");

        auto controls = AudioControls {audio.playing.load(), audio.gain.load()};
        webView.evaluateJavaScript("window.demoUpdateAudio("
                                   + Miro::toJSONString(controls) + ");");
    }

    void applyDevice(unsigned int deviceId)
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
        state.gain = audio.gain.load();
        state.sampleRate = config.sampleRate;
        state.blockSize = config.maxBlockSize;
        state.currentDeviceId = config.output ? config.output->device.id : 0;

        for (auto& device: manager.getDevices())
        {
            if (device.outputChannels == 0)
                continue;

            state.outputDevices.push_back(
                DeviceOption {device.id, device.name, device.sampleRates});
        }

        state.midiInputPorts = midi.getInputPorts();
        state.currentMidiPortId = currentMidiPortId;
        lastInputPorts = state.midiInputPorts;

        webView.evaluateJavaScript("window.demoSetState(" + Miro::toJSONString(state)
                                   + ");");
    }

    void pollMidiPorts()
    {
        auto current = midi.getInputPorts();

        if (current == lastInputPorts)
            return;

        lastInputPorts = current;

        auto state = MidiPortsState {std::move(current), currentMidiPortId};
        webView.evaluateJavaScript("window.demoSetMidiPorts("
                                   + Miro::toJSONString(state) + ");");
    }

    AudioState audio;
    MakeASound::DeviceManager manager;
    MakeASound::MidiManager midi;
    MakeASound::StreamConfig config;
    int currentMidiPortId {-1};
    std::vector<MakeASound::MidiPortInfo> lastInputPorts;
    eacp::Graphics::WebView webView {eacp::Graphics::embeddedOptions("DemoWeb")};
    eacp::Graphics::Window window;
    eacp::Threads::Timer midiPollTimer {[this] { pollMidiPorts(); }, 2};
};

int main()
{
    eacp::Apps::run<DemoApp>();
    return 0;
}
