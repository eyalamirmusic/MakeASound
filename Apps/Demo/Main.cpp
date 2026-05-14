#include <MakeASound/MakeASound.h>
#include <eacp/Core/Core.h>
#include <eacp/WebView/WebView.h>
#include <WebResources.h>

#include <atomic>
#include <random>
#include <string>
#include <utility>

namespace MS = MakeASound;
namespace eg = eacp::Graphics;

namespace
{
float nextNoiseSample()
{
    static auto engine = std::default_random_engine {std::random_device {}()};
    static auto dist = std::uniform_real_distribution<float> {-1.0f, 1.0f};
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
            sample = nextNoiseSample() * gain;
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

struct MidiPortToggleRequest
{
    MIRO_REFLECT(id, on)

    int id {};
    bool on {};
};
} // namespace

struct DemoApp
{
    DemoApp()
    {
        eg::setApplicationMenuBar(eg::buildDefaultWebViewMenuBar());

        config = manager.getDefaultConfig();
        manager.start(config, [this](auto& info) { renderWhiteNoise(info, audio); });

        registerCommands();
        window.setContentView(webView);
    }

    void registerCommands()
    {
        auto& bridge = transport.getBridge();

        bridge.on<Miro::EmptyValue, UIState>(
            "getState",
            std::function<UIState(const Miro::EmptyValue&)>(
                [this](const auto&) { return makeState(); }));

        bindVoid<bool>(
            "setPlaying", [this](bool value) { audio.playing.store(value); });

        bindVoid<double>("setGain",
                         [this](double value)
                         { audio.gain.store(static_cast<float>(value)); });

        bindVoid<int>("setSampleRate",
                      [this](int value)
                      {
                          config.sampleRate = value;
                          manager.setConfig(config);
                      });

        bindVoid<int>("setBlockSize",
                      [this](int value)
                      {
                          config.maxBlockSize = value;
                          manager.setConfig(config);
                      });

        bindVoid<int>("setDevice", [this](int id) { applyDevice(id); });

        bindVoid<MidiPortToggleRequest>(
            "midiPortToggle",
            [this](const MidiPortToggleRequest& req)
            { applyMidiPortToggle(req.id, req.on); });
    }

    template <typename Req>
    void bindVoid(const std::string& name, auto handler)
    {
        using Res = Miro::EmptyValue;
        transport.getBridge().on<Req, Res>(
            name,
            std::function<Res(const Req&)>(
                [handler = std::move(handler)](const Req& req) -> Res
                {
                    handler(req);
                    return {};
                }));
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
            broadcastState();
            return;
        }
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

        eacp::Threads::callAsync([this, msg] { publishMidi(msg); });
    }

    void publishMidi(const MS::MidiMessage& msg)
    {
        auto& bridge = transport.getBridge();
        bridge.emit("midi", MS::formatMessage(msg));
        bridge.emit("audio", makeControls());
    }

    AudioControls makeControls() const
    {
        return {.playing = audio.playing.load(),
                .gain = static_cast<double>(audio.gain.load())};
    }

    UIState makeState()
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
        return state;
    }

    void broadcastState() { transport.getBridge().emit("state", makeState()); }

    void pollMidiPorts()
    {
        auto current = midi.getInputPorts();

        if (current == lastInputPorts)
            return;

        lastInputPorts = std::move(current);
        transport.getBridge().emit("midiPorts", uiMidi.makeInputPortToggleList());
    }

    AudioState audio;
    MS::DeviceManager manager;
    MS::MidiManager midi;
    MS::UIDeviceManager uiDevices {manager};
    MS::UIMidiManager uiMidi {midi};
    MS::StreamConfig config;
    MS::Vector<MS::MidiPortInfo> lastInputPorts;
    eg::WebView webView {eg::embeddedOptions("DemoWeb")};
    eg::WebViewBridge transport {webView};
    eg::Window window;
    eacp::Threads::Timer midiPollTimer {[this] { pollMidiPorts(); }, 2};
};

int main()
{
    eacp::Apps::run<DemoApp>();
    return 0;
}
