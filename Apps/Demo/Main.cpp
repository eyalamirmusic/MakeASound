#include <MakeASound/MakeASound.h>
#include <eacp/WebView/WebView.h>
#include <WebResources.h>

#include <atomic>
#include <random>
#include <string>
#include <vector>

namespace MS = MakeASound;

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

void renderWhiteNoise(MS::AudioCallbackInfo& info, AudioState& state)
{
    auto playing = state.playing.load(std::memory_order_relaxed);
    auto gain = state.gain.load(std::memory_order_relaxed);

    for (auto channel = 0; channel < info.numOutputs; ++channel)
    {
        auto channelData = info.getOutput(channel);

        if (!playing)
        {
            std::ranges::fill(channelData, 0.0f);
            continue;
        }

        for (auto& sample: channelData)
            sample = getRandomFloat() * gain;
    }
}

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
    MS::UI::DropdownInfo devices;
    MS::UI::DropdownInfo sampleRates;
    MS::UI::ToggleListInfo midiPorts;
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
            audio.playing.store(obj["value"]);
        else if (kind == "gain")
            audio.gain.store(obj["value"]);
        else if (kind == "sampleRate")
            applySampleRate(obj["value"]);
        else if (kind == "blockSize")
            applyBlockSize(obj["value"]);
        else if (kind == "device")
            applyDevice(obj["id"]);
        else if (kind == "midiPortToggle")
            applyMidiPortToggle(obj["id"], obj["on"]);
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

    void applyMidiPortToggle(int portId, bool on)
    {
        if (on)
            midi.openInput(portId,
                           [this](const MS::MidiMessage& msg)
                           { handleIncomingMidi(msg); });
        else
            midi.closeInput(portId);
    }

    void handleIncomingMidi(const MS::MidiMessage& msg)
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

    void publishMidiToJS(const MS::MidiMessage& msg)
    {
        auto text = MS::formatMessage(msg);
        webView.evaluateJavaScript("window.demoMidiEvent(" + Miro::toJSONString(text)
                                   + ");");

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

            config.output = MS::StreamParameters {device, false};

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
        state.devices = uiDevices.makeOutputDeviceDropdown(currentDeviceId);

        if (config.output)
            state.sampleRates =
                uiDevices.makeSampleRateDropdown(currentDeviceId, config.sampleRate);

        lastInputPorts = midi.getInputPorts();
        state.midiPorts = uiMidi.makeInputPortToggleList();

        webView.evaluateJavaScript("window.demoSetState(" + Miro::toJSONString(state)
                                   + ");");
    }

    void pollMidiPorts()
    {
        auto current = midi.getInputPorts();

        if (current == lastInputPorts)
            return;

        lastInputPorts = std::move(current);

        auto info = uiMidi.makeInputPortToggleList();
        webView.evaluateJavaScript("window.demoSetMidiPorts("
                                   + Miro::toJSONString(info) + ");");
    }

    AudioState audio;
    MS::DeviceManager manager;
    MS::MidiManager midi;
    MS::UIDeviceManager uiDevices {manager};
    MS::UIMidiManager uiMidi {midi};
    MS::StreamConfig config;
    MS::Vector<MS::MidiPortInfo> lastInputPorts;
    eacp::Graphics::WebView webView {eacp::Graphics::embeddedOptions("DemoWeb")};
    eacp::Graphics::Window window;
    eacp::Threads::Timer midiPollTimer {[this] { pollMidiPorts(); }, 2};
};

int main()
{
    eacp::Apps::run<DemoApp>();
    return 0;
}
