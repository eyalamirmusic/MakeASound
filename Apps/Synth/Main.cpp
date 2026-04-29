#include <MakeASound/MakeASound.h>
#include <eacp/WebView/WebView.h>
#include <eacp/Core/Threads/Timer.h>
#include <WebResources.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace
{
constexpr auto twoPi = 2.0f * std::numbers::pi_v<float>;

float midiNoteToFrequency(int note)
{
    return 440.0f * std::pow(2.0f, static_cast<float>(note - 69) / 12.0f);
}

struct AudioState
{
    std::atomic<int> note {-1};
    std::atomic<float> velocity {0.0f};
    std::atomic<float> gain {0.5f};
};

struct SineVoice
{
    float renderSample(float increment)
    {
        auto value = std::sin(phase);
        phase += increment;

        if (phase >= twoPi)
            phase -= twoPi;

        return value;
    }

    float phase {0.0f};
};

void renderSine(MakeASound::AudioCallbackInfo& info,
                AudioState& state,
                SineVoice& voice)
{
    auto note = state.note.load(std::memory_order_relaxed);
    auto velocity = state.velocity.load(std::memory_order_relaxed);
    auto gain = state.gain.load(std::memory_order_relaxed);

    if (info.dirty)
        voice.phase = 0.0f;

    if (note < 0 || info.numOutputs <= 0)
    {
        for (auto channel = 0; channel < info.numOutputs; ++channel)
        {
            auto out = info.getOutput<float>(channel);
            std::fill_n(out, info.numSamples, 0.0f);
        }
        return;
    }

    auto frequency = midiNoteToFrequency(note);
    auto increment = twoPi * frequency / static_cast<float>(info.sampleRate);
    auto amplitude = gain * velocity;

    auto first = info.getOutput<float>(0);
    for (auto sample = 0; sample < info.numSamples; ++sample)
        first[sample] = voice.renderSample(increment) * amplitude;

    for (auto channel = 1; channel < info.numOutputs; ++channel)
    {
        auto out = info.getOutput<float>(channel);
        std::copy_n(first, info.numSamples, out);
    }
}

struct AudioControls
{
    MIRO_REFLECT(playing, gain, note, frequency, velocity)

    bool playing {};
    double gain {};
    int note {-1};
    double frequency {};
    double velocity {};
};

struct UIState
{
    MIRO_REFLECT(playing,
                 gain,
                 note,
                 frequency,
                 velocity,
                 blockSize,
                 devices,
                 sampleRates,
                 midiPorts)

    bool playing {};
    double gain {};
    int note {-1};
    double frequency {};
    double velocity {};
    int blockSize {};
    MakeASound::UI::DropdownInfo devices;
    MakeASound::UI::DropdownInfo sampleRates;
    MakeASound::UI::DropdownInfo midiPorts;
};

AudioControls makeControls(const AudioState& audio)
{
    auto note = audio.note.load();
    auto velocity = audio.velocity.load();

    auto controls = AudioControls {};
    controls.playing = note >= 0;
    controls.gain = static_cast<double>(audio.gain.load());
    controls.note = note;
    controls.velocity = static_cast<double>(velocity);
    controls.frequency =
        note >= 0 ? static_cast<double>(midiNoteToFrequency(note)) : 0.0;
    return controls;
}
} // namespace

struct SynthApp
{
    SynthApp()
    {
        config = manager.getDefaultConfig();
        manager.start(config,
                      [this](auto& info) { renderSine(info, audio, voice); });

        webView.addScriptMessageHandler(
            "synth", [this](const std::string& msg) { handleMessage(msg); });

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
        else if (kind == "allNotesOff")
            releaseAllNotes();
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
        releaseAllNotes();

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

    void noteOn(int note, float velocity)
    {
        std::erase(heldNotes, note);
        heldNotes.push_back(note);
        audio.note.store(note);
        audio.velocity.store(velocity);
    }

    void noteOff(int note)
    {
        std::erase(heldNotes, note);

        if (heldNotes.empty())
        {
            audio.note.store(-1);
            audio.velocity.store(0.0f);
        }
        else
        {
            audio.note.store(heldNotes.back());
        }
    }

    void releaseAllNotes()
    {
        heldNotes.clear();
        audio.note.store(-1);
        audio.velocity.store(0.0f);
    }

    void handleIncomingMidi(const MakeASound::MidiMessage& msg)
    {
        if (msg.bytes.size() >= 3)
        {
            auto status = msg.bytes[0] & 0xF0;
            auto data1 = static_cast<int>(msg.bytes[1]);
            auto data2 = static_cast<int>(msg.bytes[2]);

            if (status == 0x90 && data2 > 0)
                noteOn(data1, static_cast<float>(data2) / 127.0f);
            else if (status == 0x80 || (status == 0x90 && data2 == 0))
                noteOff(data1);
            else if (status == 0xB0 && data1 == 123)
                releaseAllNotes();
            else if (status == 0xB0 && data1 == 7)
                audio.gain.store(static_cast<float>(data2) / 127.0f);
        }

        eacp::Threads::callAsync([this, msg]() { publishMidiToJS(msg); });
    }

    void publishMidiToJS(const MakeASound::MidiMessage& msg)
    {
        auto text = MakeASound::formatMessage(msg);
        webView.evaluateJavaScript("window.synthMidiEvent("
                                   + Miro::toJSONString(text) + ");");

        webView.evaluateJavaScript("window.synthUpdateAudio("
                                   + Miro::toJSONString(makeControls(audio)) + ");");
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
        auto controls = makeControls(audio);

        auto state = UIState {};
        state.playing = controls.playing;
        state.gain = controls.gain;
        state.note = controls.note;
        state.frequency = controls.frequency;
        state.velocity = controls.velocity;
        state.blockSize = config.maxBlockSize;

        auto currentDeviceId = config.output ? config.output->device.id : 0;
        state.devices = MakeASound::UI::makeOutputDeviceDropdown(manager.getDevices(),
                                                                 currentDeviceId);

        if (config.output)
            state.sampleRates = MakeASound::UI::makeSampleRateDropdown(
                config.output->device, config.sampleRate);

        lastInputPorts = midi.getInputPorts();
        state.midiPorts =
            MakeASound::UI::makeMidiPortDropdown(lastInputPorts, currentMidiPortId);

        webView.evaluateJavaScript("window.synthSetState(" + Miro::toJSONString(state)
                                   + ");");
    }

    void pollMidiPorts()
    {
        auto current = midi.getInputPorts();

        if (current == lastInputPorts)
            return;

        lastInputPorts = std::move(current);

        auto info = MakeASound::UI::makeMidiPortDropdown(lastInputPorts,
                                                         currentMidiPortId);
        webView.evaluateJavaScript("window.synthSetMidiPorts("
                                   + Miro::toJSONString(info) + ");");
    }

    AudioState audio;
    SineVoice voice;
    std::vector<int> heldNotes;
    MakeASound::DeviceManager manager;
    MakeASound::MidiManager midi;
    MakeASound::StreamConfig config;
    int currentMidiPortId {-1};
    MakeASound::Vector<MakeASound::MidiPortInfo> lastInputPorts;
    eacp::Graphics::WebView webView {eacp::Graphics::embeddedOptions("SynthWeb")};
    eacp::Graphics::Window window;
    eacp::Threads::Timer midiPollTimer {[this] { pollMidiPorts(); }, 2};
};

int main()
{
    eacp::Apps::run<SynthApp>();
    return 0;
}
