// The standalone core, which needs no window: StandaloneProcessor driven by
// Engine with a synthetic AudioCallbackInfo, the sender thread over a virtual-port
// loopback, the settings file and StandaloneHost.

#include "TestPlugins.h"
#include "JuceFixtures.h"

#include <MakeASound/Plugin/Standalone/StandaloneProcessor.h>
#include <MakeASound/Plugin/Standalone/MidiSender.h>
#include <MakeASound/Plugin/Standalone/Settings.h>
#include <MakeASound/Plugin/Standalone/StandaloneHost.h>

#include <NanoTest/NanoTest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
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
{
    return static_cast<float>(channel * 100 + sample + 1);
}

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

// Settings.

// A folder of its own per instance: ctest runs every case in a process of its
// own, in parallel under -j, and one shared folder had each case's constructor
// and destructor delete the files another case was in the middle of reading.
std::filesystem::path uniqueTempDirectory()
{
    static auto counter = std::atomic<unsigned> {0};
    static auto processTag = std::random_device {}();

    auto name = "MakeASoundStandaloneTests-" + std::to_string(processTag) + "-"
                + std::to_string(counter++);
    return std::filesystem::temp_directory_path() / name;
}

struct TempFile
{
    TempFile() { std::filesystem::remove_all(directory); }

    ~TempFile() { std::filesystem::remove_all(directory); }

    std::filesystem::path directory = uniqueTempDirectory();
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

// StandaloneHost, never started unless a case says so, its settings in a
// temporary file.

GainPlugin& gainOf(StandaloneHost& host)
{
    return static_cast<GainPlugin&>(host.plugin());
}

auto tHostRoundTrip = test("Standalone/hostRestoresTheSessionItSaved") = []
{
    auto file = TempFile {};

    {
        auto host = StandaloneHost {EA::makeOwned<GainPlugin>(), file.path};
        check(host.settingsFile() == file.path);
        check(host.wrapper().plugin().format() == PluginFormat::Standalone);

        gainOf(host).params.gain.setValue(-6.f);
        gainOf(host).params.width.setValue(0.9f);
    }

    auto host = StandaloneHost {EA::makeOwned<GainPlugin>(), file.path};

    check(gainOf(host).params.gain.getValue() == -6.f);
    check(gainOf(host).params.width.getValue() == 0.9f);
};

auto tHostKeepsTheFile = test("Standalone/hostKeepsTheSavedStreamUntilStarted") = []
{
    auto file = TempFile {};

    auto settings = Settings {};
    settings.audio.output =
        StreamParameters(deviceNamed("Speakers", 3, 2, 0), false, 2, 0);
    settings.audio.maxBlockSize = 128;
    settings.midiInputPorts = {"Keys"};
    settings.midiOutputPort = "Synth";
    check(saveSettings(file.path, settings));

    {
        auto host = StandaloneHost {EA::makeOwned<GainPlugin>(), file.path};
        gainOf(host).params.gain.setValue(-12.f);
        host.saveSettings();
    }

    auto loaded = loadSettings(file.path);

    check(loaded.has_value());
    check(loaded->audio.output.has_value());
    check(loaded->audio.output->device.name == "Speakers");
    check(loaded->audio.maxBlockSize == 128);
    check(loaded->midiInputPorts == settings.midiInputPorts);
    check(loaded->midiOutputPort == "Synth");
    check(!loaded->pluginState.empty());
    check(loaded->pluginState != settings.pluginState);
};

auto tHostReset = test("Standalone/hostResetPluginRestoresTheDefaults") = []
{
    auto file = TempFile {};
    auto host = StandaloneHost {EA::makeOwned<GainPlugin>(), file.path};
    auto& params = gainOf(host).params;

    params.gain.setValue(-6.f);
    params.mode.setValue(2.f);
    params.width.setValue(0.9f);

    host.resetPlugin();

    check(params.gain.getValue() == 0.f);
    check(params.mode.getValue() == 0.f);

    // Session-only: a preset leaves it where it was.
    check(params.width.getValue() == 0.9f);

    // The reset was saved at once.
    auto loaded = loadSettings(file.path);
    check(loaded.has_value());
    check(!loaded->pluginState.empty());
};

auto tHostNoFile = test("Standalone/hostWithNoPathWritesNoFile") = []
{
    auto file = TempFile {};

    {
        auto host = StandaloneHost {EA::makeOwned<GainPlugin>(), {}};
        check(host.settingsFile().empty());
        host.saveSettings();
        host.resetPlugin();
    }

    check(!std::filesystem::exists(file.directory));
};

auto tHostLayout = test("Standalone/hostReportsThePluginsLayout") = []
{
    auto effect = StandaloneHost {EA::makeOwned<GainPlugin>(), {}};
    check(effect.busLayout() == BusLayout::stereoInOut());
    check(!effect.hasMidiInput());
    check(!effect.hasMidiOutput());

    auto instrument = StandaloneHost {EA::makeOwned<SynthPlugin>(), {}};
    check(instrument.hasMidiInput());
    check(instrument.hasMidiOutput());
    check(instrument.injectMidi(MIDI::Event::noteOn(0, 60, 1.f)));
};

auto tHostIdentity = test("Standalone/hostAppliesTheDescriptionsIdentity") = []
{
    auto native = StandaloneHost {EA::makeOwned<GainPlugin>(), {}};
    check(native.plugin().parameters().identity() == ParameterIdentity {});

    auto juce = policyFor(IdScheme::JUCE, {.studioOneCompatibleParameters = false})
                    .parameters;
    auto port = StandaloneHost {EA::makeOwned<GainPlugin>(), {}, {}, juce};
    check(port.plugin().parameters().identity() == juce);
};

auto tHostStart = test("Standalone/hostStartsOnAGoneDeviceWithoutCrashing") = []
{
    auto file = TempFile {};

    auto settings = Settings {};
    settings.audio.output =
        StreamParameters(deviceNamed("No Such Device", 42, 2, 0), false);
    settings.audio.sampleRate = 48000;
    settings.audio.maxBlockSize = 256;
    settings.midiInputPorts = {"No Such Port"};
    settings.midiOutputPort = "No Such Port";
    check(saveSettings(file.path, settings));

    {
        auto host = StandaloneHost {EA::makeOwned<GainPlugin>(), file.path};
        host.start();

        // The gone device's side took the fallback's; there may be none.
        check(!host.config().output
              || host.config().output->device.name != "No Such Device");
        check(host.openMidiInputs().empty());
        check(!host.midiOutput());

        check(host.setMidiOutput(123456) != Error::NoError);
        check(!host.midiOutput());

        host.resetAudioAndMidi();
        check(host.openMidiInputs().empty());
    }

    // Started, so the file now holds what the host settled on.
    auto loaded = loadSettings(file.path);
    check(loaded.has_value());
    check(loaded->midiInputPorts.empty());
    check(loaded->midiOutputPort.empty());
    check(!loaded->audio.output
          || loaded->audio.output->device.name != "No Such Device");
};

// What a JUCE build of the plugin's standalone saved.

using JuceFixtures::FixturePlugin;

FixturePlugin& fixtureOf(StandaloneHost& host)
{
    return static_cast<FixturePlugin&>(host.plugin());
}

// A JUCE settings file beside the native one, holding `contents`.
std::string writeJuceSettings(const TempFile& file, const std::string& contents)
{
    auto path = file.directory / "Fixture.settings";
    std::filesystem::create_directories(file.directory);
    std::ofstream(path, std::ios::binary) << contents;
    return path.string();
}

auto tJucePath = test("Standalone/juceSettingsPathIsNamedAfterTheJuceBuild") = []
{
    auto path = juceSettingsPath("Fixture");

    // The folder is the platform's; there may be none where JUCE knew none.
    check(path.empty() || path.ends_with("Fixture.settings"));
    check(juceSettingsPath("").empty());
};

auto tJuceImport = test("Standalone/hostImportsAJuceBuildsSettingsOnce") = []
{
    auto file = TempFile {};
    auto juceFile = writeJuceSettings(
        file, JuceFixtures::standaloneSettings(JuceFixtures::apvtsBase64));

    {
        auto host =
            StandaloneHost {EA::makeOwned<FixturePlugin>(), file.path, juceFile};
        check(host.juceSettingsFile() == juceFile);

        auto& plugin = fixtureOf(host);
        check(plugin.legacyLoads == 1);
        check(plugin.origin == ForeignOrigin::JuceStandalone);
        check(plugin.context == StateContext::Session);
        check(JuceFixtures::holdsFixtureValues(plugin.params));
    }

    // The host's save wrote the native file, so the next launch reads that.
    auto loaded = loadSettings(file.path);
    check(loaded.has_value());
    check(loaded->pluginState.starts_with("{"));

    auto host = StandaloneHost {EA::makeOwned<FixturePlugin>(), file.path, juceFile};
    check(fixtureOf(host).legacyLoads == 0);
    check(JuceFixtures::holdsFixtureValues(fixtureOf(host).params));
};

auto tJuceNotNeeded = test("Standalone/aNativeSettingsFileWinsOverAJuceOne") = []
{
    auto file = TempFile {};

    {
        auto host = StandaloneHost {EA::makeOwned<FixturePlugin>(), file.path};
        fixtureOf(host).params.cutoff.setValue(2000.f);
    }

    auto juceFile = writeJuceSettings(
        file, JuceFixtures::standaloneSettings(JuceFixtures::apvtsBase64));
    auto host = StandaloneHost {EA::makeOwned<FixturePlugin>(), file.path, juceFile};
    check(fixtureOf(host).legacyLoads == 0);
    check(fixtureOf(host).params.cutoff.getValue() == 2000.f);
};

auto tJuceRefused =
    test("Standalone/aJuceImportThePluginCannotReadMovesNothing") = []
{
    auto file = TempFile {};

    // No such file, a file with no state, and a state the plugin refuses.
    {
        auto host = StandaloneHost {
            EA::makeOwned<FixturePlugin>(), {}, (file.directory / "Gone").string()};
        check(fixtureOf(host).legacyLoads == 0);
    }

    auto noState = writeJuceSettings(file, "<PROPERTIES/>");
    {
        auto host = StandaloneHost {EA::makeOwned<FixturePlugin>(), {}, noState};
        check(fixtureOf(host).legacyLoads == 0);
        check(JuceFixtures::holdsDefaults(fixtureOf(host).params));
    }

    // The blob's first 12 bytes: no XML the plugin can read.
    auto truncated = writeJuceSettings(
        file, JuceFixtures::standaloneSettings("12.VMjLgfS....O+fWarAhckI2bo8la"));
    auto host = StandaloneHost {EA::makeOwned<FixturePlugin>(), {}, truncated};
    check(fixtureOf(host).legacyLoads == 1);
    check(JuceFixtures::holdsDefaults(fixtureOf(host).params));
};
} // namespace
