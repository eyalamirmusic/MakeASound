// The standalone format's pieces without a window: StandaloneProcessor driven by
// Engine with a synthetic AudioCallbackInfo, the sender thread over a virtual-port
// loopback, the typing keyboard's mapping and the settings file.

#include "TestPlugins.h"

#include <MakeASound/Plugin/Standalone/StandaloneProcessor.h>
#include <MakeASound/Plugin/Standalone/MidiSender.h>
#include <MakeASound/Plugin/Standalone/TypingKeyboard.h>
#include <MakeASound/Plugin/Standalone/Settings.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Graphics/Graphics/Keyboard.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace nano;
using namespace TestPlugins;
using namespace std::chrono_literals;
using namespace MakeASound::Standalone;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
constexpr auto sampleRate = 48000;
constexpr auto garbage = 7.f;

float inputAt(int channel, int sample) noexcept
{ return static_cast<float>(channel * 100 + sample + 1); }

struct FakeStream
{
    FakeStream(int numInputs, int numOutputs)
        : inputs(numInputs, std::vector<float>(blockSize))
        , outputs(numOutputs, std::vector<float>(blockSize, garbage))
    {
        for (auto ch = 0; ch < numInputs; ++ch)
        {
            for (auto s = 0; s < blockSize; ++s)
                inputs[ch][s] = inputAt(ch, s);

            inputTable.push_back(inputs[ch].data());
        }

        for (auto& channel: outputs)
            outputTable.push_back(channel.data());
    }

    void run(Engine& engine, bool dirty = false)
    {
        auto info = AudioCallbackInfo {};
        info.numInputs = static_cast<int>(inputs.size());
        info.numOutputs = static_cast<int>(outputs.size());
        info.inputChannels = inputTable.empty() ? nullptr : inputTable.data();
        info.outputChannels = outputTable.empty() ? nullptr : outputTable.data();
        info.numSamples = blockSize;
        info.sampleRate = sampleRate;
        info.maxBlockSize = blockSize;
        info.dirty = dirty;
        engine.process(info);
    }

    std::vector<std::vector<float>> inputs;
    std::vector<std::vector<float>> outputs;
    std::vector<float*> inputTable;
    std::vector<float*> outputTable;
};

template <typename PluginT>
struct Rig
{
    PluginT& plugin() { return static_cast<PluginT&>(wrapper.plugin()); }

    DeviceManager devices;
    MidiManager midi;
    Engine engine {devices, midi};
    MidiSender sender {midi};
    PluginWrapper wrapper {EA::makeOwned<PluginT>(), PluginFormat::Standalone};
    StandaloneProcessor processor {wrapper, sender};
};

auto tEffectBlock = test("Standalone/anEffectHearsTheInputInItsOutput") = []
{
    auto rig = Rig<GainPlugin> {};
    rig.plugin().params.gain.setValue(-6.f);
    auto gain = rig.plugin().params.gain.gain();

    auto stream = FakeStream {2, 4};

    rig.engine.prepare(rig.processor, sampleRate, blockSize);
    stream.run(rig.engine, true);

    auto& plugin = rig.plugin();
    check(plugin.processed == 1);
    check(plugin.seenInput[0][0] == inputAt(0, 0));
    check(plugin.seenInput[1][blockSize - 1] == inputAt(1, blockSize - 1));

    // The wrapper's copy reached the plugin before its gain did.
    check(plugin.seenOutput[0][3] == inputAt(0, 3));

    auto scaled = true;

    for (auto ch = 0; ch < 2; ++ch)
        for (auto s = 0; s < blockSize; ++s)
            scaled = scaled && stream.outputs[ch][s] == inputAt(ch, s) * gain;

    auto cleared = true;

    for (auto ch = 2; ch < 4; ++ch)
        for (auto s = 0; s < blockSize; ++s)
            cleared = cleared && stream.outputs[ch][s] == 0.f;

    check(scaled);
    check(cleared);
};

auto tEffectPrepare = test("Standalone/prepareReachesThePluginAsStandalone") = []
{
    auto rig = Rig<GainPlugin> {};

    check(rig.processor.getBusLayout() == rig.wrapper.busLayout());

    rig.engine.prepare(rig.processor, sampleRate, blockSize);

    auto& plugin = rig.plugin();
    check(plugin.formatAtPrepare == PluginFormat::Standalone);
    check(plugin.spec.sampleRate == sampleRate);
    check(plugin.spec.maxBlockSize == blockSize);
    check(plugin.spec.layout == BusLayout::stereoInOut());
};

auto tEffectReset = test("Standalone/aDirtyBlockResetsThePlugin") = []
{
    auto rig = Rig<GainPlugin> {};
    auto stream = FakeStream {2, 2};

    rig.engine.prepare(rig.processor, sampleRate, blockSize);
    stream.run(rig.engine, true);
    stream.run(rig.engine);

    check(rig.plugin().resets == 1);

    stream.run(rig.engine, true);

    check(rig.plugin().resets == 2);
    check(rig.plugin().processed == 3);
};

auto tMonoMic = test("Standalone/aMonoMicReachesBothSidesOfTheBus") = []
{
    auto rig = Rig<GainPlugin> {};
    auto stream = FakeStream {1, 2};

    rig.engine.prepare(rig.processor, sampleRate, blockSize);
    stream.run(rig.engine, true);

    check(stream.outputs[0][5] == inputAt(0, 5));
    check(stream.outputs[1][5] == inputAt(0, 5));
};

auto tInjected = test("Standalone/anInjectedEventLandsAtOffsetZero") = []
{
    auto rig = Rig<SynthPlugin> {};
    auto stream = FakeStream {0, 2};

    rig.engine.prepare(rig.processor, sampleRate, blockSize);
    stream.run(rig.engine, true);

    check(rig.processor.injectMidi(MIDI::Event::noteOn(0, 64, 1.f, 40)));
    stream.run(rig.engine);

    auto& plugin = rig.plugin();
    check(plugin.numEvents == 1);
    check(plugin.events[0].isNoteOn());
    check(plugin.events[0].asNoteOn()->pitch == 64);
    check(plugin.events[0].sampleOffset == 0);
    check(stream.outputs[0][0] == plugin.params.level.get());

    stream.run(rig.engine);
    check(plugin.numEvents == 0);
};

auto tInjectedFull = test("Standalone/injectionRefusesPastTheQueue") = []
{
    auto rig = Rig<SynthPlugin> {};
    auto accepted = 0;

    for (auto i = 0; i < 300; ++i)
        accepted +=
            rig.processor.injectMidi(MIDI::Event::noteOn(0, 60, 1.f)) ? 1 : 0;

    check(accepted == 256);
};

const std::string loopbackName = "MakeASound Standalone Test";

bool openOutputTo(MidiManager& midi, const std::string& name)
{
    for (const auto& port: midi.getOutputPorts())
        if (port.name.find(name) != std::string::npos)
            return midi.openOutput(port.id) == Error::NoError;

    return false;
}

auto tHardwareAndInjected =
    test("Standalone/hardwareAndInjectedMidiReachThePluginInOrder") = []
{
    auto rig = Rig<SynthPlugin> {};

    if (!rig.midi.isAvailable() || !rig.midi.openVirtualInput(loopbackName))
        return;

    if (!openOutputTo(rig.midi, loopbackName))
        return;

    auto stream = FakeStream {0, 2};

    rig.engine.prepare(rig.processor, sampleRate, blockSize);
    stream.run(rig.engine, true);

    auto sent = rig.midi.sendMessage(MIDI::Event::noteOn(0, 60, 1.f));
    std::this_thread::sleep_for(10ms);
    rig.processor.injectMidi(MIDI::Event::controlChange(0, 7, 0.5f, 30));

    auto hardware = 0;
    auto injected = 0;
    auto injectedAtZero = true;
    auto sorted = true;
    auto& plugin = rig.plugin();

    for (auto block = 0; block < 200 && (hardware == 0 || injected == 0); ++block)
    {
        stream.run(rig.engine);

        for (auto i = 0; i < plugin.numEvents; ++i)
        {
            const auto& event = plugin.events[i];

            if (i > 0)
                sorted = sorted
                         && plugin.events[i - 1].sampleOffset <= event.sampleOffset;

            if (event.isNoteOn())
                ++hardware;

            if (event.isControlChange())
            {
                ++injected;
                injectedAtZero = injectedAtZero && event.sampleOffset == 0;
            }
        }

        std::this_thread::sleep_for(2ms);
    }

    rig.midi.closeOutput();
    rig.midi.closeAllInputs();

    check(injected == 1);
    check(injectedAtZero);
    check(sorted);

    if (sent == Error::NoError && hardware > 0)
        check(hardware == 1);
};

auto tMidiOut = test("Standalone/theEchoedMidiOutLeavesThroughTheSender") = []
{
    auto receiver = MidiManager {};
    auto noteOns = std::atomic<int> {0};
    auto receiverName = loopbackName + " Receiver";

    if (!receiver.isAvailable())
        return;

    auto opened = receiver.openVirtualInput(
        receiverName,
        [&](const MidiMessage& message)
        {
            if (!message.bytes.empty() && (message.bytes[0] & 0xF0) == 0x90)
                ++noteOns;
        });

    if (!opened)
        return;

    auto rig = Rig<SynthPlugin> {};

    if (!openOutputTo(rig.midi, receiverName))
        return;

    auto stream = FakeStream {0, 2};

    rig.engine.prepare(rig.processor, sampleRate, blockSize);
    stream.run(rig.engine, true);

    rig.sender.start();
    rig.sender.start();
    check(rig.sender.isRunning());

    rig.processor.injectMidi(MIDI::Event::noteOn(0, 67, 1.f));
    stream.run(rig.engine);

    for (auto wait = 0; wait < 400 && noteOns == 0; ++wait)
        std::this_thread::sleep_for(5ms);

    rig.sender.stop();
    rig.sender.stop();
    check(!rig.sender.isRunning());

    rig.midi.closeOutput();
    receiver.closeAllInputs();

    check(noteOns == 1);
};

auto tSenderNoOutput = test("Standalone/theSenderDropsWithNoOutputOpen") = []
{
    auto midi = MidiManager {};
    auto sender = MidiSender {midi};

    sender.start();

    auto accepted = 0;

    for (auto i = 0; i < 10; ++i)
        accepted += sender.push(MIDI::Event::noteOn(0, 60, 1.f)) ? 1 : 0;

    std::this_thread::sleep_for(20ms);
    sender.stop();

    check(accepted == 10);
    check(!midi.isOutputOpen());
};

// The typing keyboard.

namespace Key = eacp::Graphics::KeyCode;

struct Typist
{
    TypingKeyboard keyboard {[this](const MIDI::Event& event)
                             {
                                 if (accepting)
                                     events.push_back(event);

                                 return accepting;
                             }};
    std::vector<MIDI::Event> events;
    bool accepting = true;
};

bool isNoteOn(const MIDI::Event& event, int pitch)
{ return event.isNoteOn() && event.asNoteOn()->pitch == pitch; }

bool isNoteOff(const MIDI::Event& event, int pitch)
{ return event.isNoteOff() && event.asNoteOff()->pitch == pitch; }

auto tKeyMap = test("Standalone/typingKeyboardMapsEveryKey") = []
{
    auto keys = std::array<uint16_t, 17> {Key::A,
                                          Key::W,
                                          Key::S,
                                          Key::E,
                                          Key::D,
                                          Key::F,
                                          Key::T,
                                          Key::G,
                                          Key::Y,
                                          Key::H,
                                          Key::U,
                                          Key::J,
                                          Key::K,
                                          Key::O,
                                          Key::L,
                                          Key::P,
                                          Key::Semicolon};

    auto mapped = true;

    for (auto i = 0; i < static_cast<int>(keys.size()); ++i)
        mapped = mapped && TypingKeyboard::semitoneForKey(keys[i]) == i;

    check(mapped);
    check(!TypingKeyboard::semitoneForKey(Key::Q));
    check(!TypingKeyboard::semitoneForKey(Key::Z));
    check(!TypingKeyboard::semitoneForKey(Key::X));
};

auto tKeyDown = test("Standalone/typingKeyboardPlaysAtTheBaseNote") = []
{
    auto typist = Typist {};

    check(typist.keyboard.baseNote() == 60);
    check(typist.keyboard.keyDown(Key::A, false, false));
    check(typist.keyboard.keyDown(Key::Semicolon, false, false));

    check(typist.events.size() == 2);
    check(isNoteOn(typist.events[0], 60));
    check(isNoteOn(typist.events[1], 76));
    check(typist.events[0].asNoteOn()->velocity == 0.8f);
    check(typist.events[0].channel == 0);

    check(typist.keyboard.keyUp(Key::A));
    check(typist.events.size() == 3);
    check(isNoteOff(typist.events[2], 60));
};

auto tKeyIgnored = test("Standalone/typingKeyboardIgnoresRepeatsAndChords") = []
{
    auto typist = Typist {};

    check(!typist.keyboard.keyDown(Key::A, true, false));
    check(!typist.keyboard.keyDown(Key::A, false, true));
    check(!typist.keyboard.keyDown(Key::Z, false, true));
    check(typist.events.empty());
    check(typist.keyboard.baseNote() == 60);

    check(typist.keyboard.keyDown(Key::A, false, false));
    check(!typist.keyboard.keyDown(Key::A, false, false));
    check(typist.events.size() == 1);

    check(!typist.keyboard.keyDown(Key::Q, false, false));
    check(!typist.keyboard.keyUp(Key::Q));
    check(typist.events.size() == 1);
};

auto tKeyShift = test("Standalone/typingKeyboardReleasesTheNoteItPlayed") = []
{
    auto typist = Typist {};

    typist.keyboard.keyDown(Key::D, false, false);
    check(typist.keyboard.keyDown(Key::Z, false, false));
    check(typist.keyboard.baseNote() == 48);

    typist.keyboard.keyUp(Key::D);
    typist.keyboard.keyDown(Key::D, false, false);

    check(typist.events.size() == 3);
    check(isNoteOn(typist.events[0], 64));
    check(isNoteOff(typist.events[1], 64));
    check(isNoteOn(typist.events[2], 52));
};

auto tKeyAllOff = test("Standalone/typingKeyboardAllNotesOffReleasesEachOnce") = []
{
    auto typist = Typist {};

    typist.keyboard.keyDown(Key::A, false, false);
    typist.keyboard.keyDown(Key::X, false, false);
    typist.keyboard.keyDown(Key::K, false, false);
    typist.keyboard.allNotesOff();

    check(typist.events.size() == 4);
    check(isNoteOff(typist.events[2], 60));
    check(isNoteOff(typist.events[3], 84));

    typist.keyboard.allNotesOff();
    typist.keyboard.keyUp(Key::A);
    check(typist.events.size() == 4);
};

auto tKeyClamp = test("Standalone/typingKeyboardClampsTheOctave") = []
{
    auto typist = Typist {};

    for (auto i = 0; i < 12; ++i)
        typist.keyboard.keyDown(Key::Z, false, false);

    check(typist.keyboard.baseNote() == 0);

    typist.keyboard.keyDown(Key::A, false, false);
    check(isNoteOn(typist.events.back(), 0));

    for (auto i = 0; i < 12; ++i)
        typist.keyboard.keyDown(Key::X, false, false);

    check(typist.keyboard.baseNote() == 111);

    typist.keyboard.keyDown(Key::Semicolon, false, false);
    check(isNoteOn(typist.events.back(), 127));
};

// Settings.

struct TempFile
{
    TempFile() { std::filesystem::remove_all(directory); }

    ~TempFile() { std::filesystem::remove_all(directory); }

    std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "MakeASoundStandaloneTests";
    std::string path = (directory / "nested" / "settings.json").string();
};

DeviceInfo deviceNamed(const std::string& name, int id, int outputs, int inputs)
{
    auto device = DeviceInfo {};
    device.id = id;
    device.name = name;
    device.outputChannels = outputs;
    device.inputChannels = inputs;
    device.sampleRates = {44100, 48000};
    device.currentSampleRate = 44100;
    device.preferredSampleRate = 48000;
    return device;
}

auto tDroppedNote = test("Standalone/typingKeyboardOwesNothingForADroppedNote") = []
{
    auto typist = Typist {};
    typist.accepting = false;

    check(!typist.keyboard.keyDown(Key::A, false, false));

    typist.accepting = true;
    typist.keyboard.keyUp(Key::A);
    check(typist.events.empty());

    check(typist.keyboard.keyDown(Key::A, false, false));
    typist.accepting = false;
    typist.keyboard.keyUp(Key::A);
    typist.accepting = true;
    typist.keyboard.allNotesOff();

    check(typist.events.size() == 2);
    check(isNoteOff(typist.events[1], 60));
};

auto tSettingsRoundTrip = test("Standalone/settingsRoundTripThroughAFile") = []
{
    auto file = TempFile {};

    auto settings = Settings {};
    settings.audio.output =
        StreamParameters(deviceNamed("Speakers", 3, 2, 0), false, 2, 0);
    settings.audio.sampleRate = 44100;
    settings.audio.maxBlockSize = 128;
    settings.midiInputPorts = {"Keys", "Pads"};
    settings.midiOutputPort = "Synth";
    settings.pluginState = R"({"version":1,"params":{"Gain":-6}})";

    check(saveSettings(file.path, settings));

    auto loaded = loadSettings(file.path);

    check(loaded.has_value());
    check(loaded->version == 1);
    check(loaded->audio.output.has_value());
    check(loaded->audio.output->device.name == "Speakers");
    check(loaded->audio.output->nChannels == 2);
    check(!loaded->audio.input.has_value());
    check(loaded->audio.sampleRate == 44100);
    check(loaded->audio.maxBlockSize == 128);
    check(loaded->midiInputPorts == settings.midiInputPorts);
    check(loaded->midiOutputPort == "Synth");
    check(loaded->pluginState == settings.pluginState);
};

auto tSettingsMissing = test("Standalone/settingsMissingOrGarbageAreNullopt") = []
{
    auto file = TempFile {};

    check(!loadSettings(file.path).has_value());

    std::filesystem::create_directories(file.directory / "nested");

    auto write = [&](const std::string& text)
    { std::ofstream(file.path, std::ios::trunc) << text; };

    write("");
    check(!loadSettings(file.path).has_value());

    write("  \n ");
    check(!loadSettings(file.path).has_value());

    write("not json {");
    check(!loadSettings(file.path).has_value());

    write("[1, 2]");
    check(!loadSettings(file.path).has_value());
};

auto tSettingsPath = test("Standalone/settingsPathIsUnderTheVendorAndPlugin") = []
{
    auto path = settingsPath("Acme", "Gain");

    check(path.ends_with("Acme/Gain/settings.json"));
};

StreamConfig savedOutput(const DeviceInfo& device, int channels, int first)
{
    auto config = StreamConfig {};
    config.output = StreamParameters(device, false, channels, first);
    config.sampleRate = 48000;
    config.maxBlockSize = 256;
    return config;
}

StreamConfig fallbackConfig()
{
    auto config = StreamConfig {};
    config.output = StreamParameters(deviceNamed("Built-in", 1, 2, 0), false);
    config.sampleRate = 44100;
    config.maxBlockSize = 512;
    return config;
}

auto tResolveRepoint = test("Standalone/resolveConfigRepointsByName") = []
{
    auto earlier = deviceNamed("Interface", 9, 4, 2);
    auto devices = Vector<DeviceInfo> {deviceNamed("Built-in", 1, 2, 0),
                                       deviceNamed("Interface", 2, 4, 2)};

    auto resolved =
        resolveConfig(savedOutput(earlier, 2, 2), devices, fallbackConfig());

    check(resolved.output.has_value());
    check(resolved.output->device.id == 2);
    check(resolved.output->nChannels == 2);
    check(resolved.output->firstChannel == 2);
    check(!resolved.input.has_value());
    check(resolved.sampleRate == 48000);
    check(resolved.maxBlockSize == 256);
};

auto tResolveGone = test("Standalone/resolveConfigFallsBackForAMissingDevice") = []
{
    auto devices = Vector<DeviceInfo> {deviceNamed("Built-in", 1, 2, 0)};
    auto saved = savedOutput(deviceNamed("Gone", 4, 2, 0), 2, 0);
    saved.input = StreamParameters(deviceNamed("Gone Mic", 5, 0, 1), true);

    auto resolved = resolveConfig(saved, devices, fallbackConfig());

    check(resolved.output.has_value());
    check(resolved.output->device.name == "Built-in");
    check(!resolved.input.has_value());
    check(resolved.maxBlockSize == 256);
};

auto tResolveRate = test("Standalone/resolveConfigReplacesARateNotOffered") = []
{
    auto devices = Vector<DeviceInfo> {deviceNamed("Interface", 2, 4, 2)};
    auto saved = savedOutput(devices[0], 2, 0);
    saved.sampleRate = 96000;

    auto resolved = resolveConfig(saved, devices, fallbackConfig());

    check(resolved.sampleRate == 44100);
};

auto tResolveClamp = test("Standalone/resolveConfigClampsTheChannelSpan") = []
{
    auto devices = Vector<DeviceInfo> {deviceNamed("Interface", 2, 4, 2)};

    auto shifted = resolveConfig(savedOutput(devices[0], 2, 6), devices, {});
    check(shifted.output->nChannels == 2);
    check(shifted.output->firstChannel == 2);

    auto narrowed = resolveConfig(savedOutput(devices[0], 8, 0), devices, {});
    check(narrowed.output->nChannels == 4);
    check(narrowed.output->firstChannel == 0);
};

auto tResolveEmpty = test("Standalone/resolveConfigWithNoSideIsTheFallback") = []
{
    auto resolved = resolveConfig({}, {}, fallbackConfig());

    check(resolved.output.has_value());
    check(resolved.output->device.name == "Built-in");
    check(resolved.sampleRate == 44100);
    check(resolved.maxBlockSize == 512);
};

auto tResolvePorts = test("Standalone/resolvePortIdsMatchesByName") = []
{
    auto ports = Vector<MidiPortInfo> {{4, "Keys"}, {7, "Pads"}, {9, "Drums"}};
    auto ids = resolvePortIds({"Pads", "Gone", "Keys"}, ports);

    check(ids == Vector<int> {7, 4});
};
} // namespace
