#include <MakeASound/MakeASound.h>
#include <eacp/WebView/WebView.h>
#include <eacp/Core/Threads/Timer.h>
#include <WebResources.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
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

void renderSegment(MakeASound::AudioCallbackInfo& info,
                   const AudioState& state,
                   SineVoice& voice,
                   int startSample,
                   int endSample)
{
    if (startSample >= endSample || info.numOutputs <= 0)
        return;

    auto note = state.note.load(std::memory_order_relaxed);
    auto velocity = state.velocity.load(std::memory_order_relaxed);
    auto gain = state.gain.load(std::memory_order_relaxed);

    auto first = info.getOutput(0);

    if (note < 0)
    {
        std::fill(first.begin() + startSample, first.begin() + endSample, 0.0f);
    }
    else
    {
        auto frequency = midiNoteToFrequency(note);
        auto increment = twoPi * frequency / static_cast<float>(info.sampleRate);
        auto amplitude = gain * velocity;

        for (auto i = static_cast<std::size_t>(startSample);
             i < static_cast<std::size_t>(endSample);
             ++i)
            first[i] = voice.renderSample(increment) * amplitude;
    }

    for (auto channel = 1; channel < info.numOutputs; ++channel)
    {
        auto out = info.getOutput(channel);
        std::copy(first.begin() + startSample,
                  first.begin() + endSample,
                  out.begin() + startSample);
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
    MakeASound::UI::ToggleListInfo midiPorts;
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
        config.input.reset();
        manager.start(config, [this](auto& info) { audioCallback(info); });

        webView.addScriptMessageHandler(
            "synth", [this](const std::string& msg) { handleMessage(msg); });

        window.setContentView(webView);
    }

    void audioCallback(MakeASound::AudioCallbackInfo& info)
    {
        if (info.dirty)
        {
            voice.phase = 0.0f;
            midiSync.reset();
        }

        auto blockStart = std::chrono::steady_clock::now();
        midiSync.drainForBlock(midi, info.numSamples, info.sampleRate);

        auto cursor = 0;

        for (auto& evt: midiSync.events())
        {
            renderSegment(info, audio, voice, cursor, evt.sampleOffset);
            applyMidiOnAudioThread(evt, blockStart, info.sampleRate);
            cursor = evt.sampleOffset;
        }

        renderSegment(info, audio, voice, cursor, info.numSamples);
    }

    void applyMidiOnAudioThread(const MakeASound::MidiInputEvent& evt,
                                MakeASound::MidiTimePoint blockStart,
                                int sampleRate)
    {
        applyMidiState(evt.message);
        logEventDiagnostic(evt, blockStart, sampleRate);

        auto msg = evt.message;
        eacp::Threads::callAsync([this, msg]() { publishMidiToJS(msg); });
    }

    void logEventDiagnostic(const MakeASound::MidiInputEvent& evt,
                            MakeASound::MidiTimePoint blockStart,
                            int sampleRate) const
    {
        using namespace std::chrono;

        auto arrivalUs =
            duration_cast<microseconds>(evt.arrival - blockStart).count();
        auto applicationUs = static_cast<long long>(
            (1'000'000.0 * evt.sampleOffset) / static_cast<double>(sampleRate));
        auto latencyUs = applicationUs - arrivalUs;

        std::printf("[synth] sampleOffset=%4d  arrival=%+7lldus  applied=%+7lldus  "
                    "latency=%+7lldus  bytes=%zu\n",
                    evt.sampleOffset,
                    static_cast<long long>(arrivalUs),
                    applicationUs,
                    latencyUs,
                    evt.message.bytes.size());
        std::fflush(stdout);
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
            audio.gain.store(obj["value"]);
        else if (kind == "sampleRate")
            applySampleRate(obj["value"]);
        else if (kind == "blockSize")
            applyBlockSize(obj["value"]);
        else if (kind == "device")
            applyDevice(obj["id"]);
        else if (kind == "midiPortToggle")
            applyMidiPortToggle(obj["id"], obj["on"]);
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

    void applyMidiPortToggle(int portId, bool on)
    {
        if (on)
        {
            midi.openInput(portId);
        }
        else
        {
            midi.closeInput(portId);
            releaseAllNotes();
        }
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

    void applyMidiState(const MakeASound::MidiMessage& msg)
    {
        if (msg.bytes.size() < 3)
            return;

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
        state.devices = uiDevices.makeOutputDeviceDropdown(currentDeviceId);

        if (config.output)
            state.sampleRates =
                uiDevices.makeSampleRateDropdown(currentDeviceId, config.sampleRate);

        lastInputPorts = midi.getInputPorts();
        state.midiPorts = uiMidi.makeInputPortToggleList();

        webView.evaluateJavaScript("window.synthSetState(" + Miro::toJSONString(state)
                                   + ");");
    }

    void pollMidiPorts()
    {
        auto current = midi.getInputPorts();

        if (current == lastInputPorts)
            return;

        lastInputPorts = std::move(current);

        auto info = uiMidi.makeInputPortToggleList();
        webView.evaluateJavaScript("window.synthSetMidiPorts("
                                   + Miro::toJSONString(info) + ");");
    }

    AudioState audio;
    SineVoice voice;
    std::vector<int> heldNotes;
    MakeASound::DeviceManager manager;
    MakeASound::MidiManager midi;
    MakeASound::UIDeviceManager uiDevices {manager};
    MakeASound::UIMidiManager uiMidi {midi};
    MakeASound::MidiBlockSync midiSync;
    MakeASound::StreamConfig config;
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
