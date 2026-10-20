// Hosting Audio Units: the test module's own AU components, registered in this
// process and handed to the loader directly (never found through the registry),
// and Apple's AUDelay found and loaded the way a host loads a scanned plugin.
// Buses, parameters (ids, value strings, text), a host edit, the unit's own
// gestures reaching the listener, a value moved behind the host's back, audio,
// MIDI in, the state round trip and the fallback playhead.

#include "AUTestHost.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/AU/AUFormat.h>
#include <MakeASound/Hosting/AU/HostedAudioUnit.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <cmath>
#include <dlfcn.h>
#include <filesystem>
#include <map>

namespace HostedAUTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;

namespace
{
constexpr auto blockSize = TestPlugins::blockSize;

struct DefaultFormats : FormatManager
{
    DefaultFormats() { addDefaultFormats(); }
};

// One of the test module's components, registered here, loaded as a host would.
OwningPointer<HostedAudioUnit> loadTestUnit(FourCC code)
{
    auto component = AUHost::registerComponent(AUHost::descriptionFor(code));

    auto description = Hosting::PluginDescription();
    description.name = std::string(AUHost::pluginFor(code).name);

    auto result = loadAudioUnit(component, description, {});
    check(result.error.empty());

    auto* unit = dynamic_cast<HostedAudioUnit*>(result.plugin.get());
    check(unit != nullptr);

    if (unit == nullptr)
        return {};

    result.plugin.release();
    return OwningPointer<HostedAudioUnit>(unit);
}

// A fixture's own component (Tests/HostFixtures), loaded out of its bundle and
// registered in this process once: its factory comes from the bundle's binary,
// and the registry is never searched.
OwningPointer<HostedPlugin>
    loadFixtureUnit(const char* name, OSType type, FourCC code)
{
    using Factory = void* (*)(const AudioComponentDescription*);
    static auto registered = std::map<std::string, AudioComponent> {};

    auto description = AudioComponentDescription {
        .componentType = type,
        .componentSubType = code.toUint32(),
        .componentManufacturer = FourCC("MkTs").toUint32()};

    if (registered.count(name) == 0)
    {
        auto binary = std::filesystem::path(MAKEASOUND_HOST_FIXTURE_AU_DIR)
                      / (std::string(name) + ".component") / "Contents" / "MacOS"
                      / name;
        auto* module = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
        check(module != nullptr);

        auto* symbol =
            module != nullptr ? dlsym(module, "MakeASoundAUFactory") : nullptr;
        auto factory = reinterpret_cast<Factory>(symbol);
        check(factory != nullptr);

        if (factory == nullptr)
            return {};

        registered[name] = AudioComponentRegister(
            &description,
            CFSTR("MakeASound Tests: Host Fixture"),
            0x10000,
            reinterpret_cast<AudioComponentFactoryFunction>(factory));
    }

    auto plugin = Hosting::PluginDescription();
    plugin.name = name;

    auto loaded = loadAudioUnit(registered[name], plugin, {});
    check(loaded.error.empty());
    return std::move(loaded.plugin);
}

OwningPointer<HostedPlugin> loadFixtureEffect()
{
    return loadFixtureUnit("HostFixtureEffect", kAudioUnitType_Effect, "HfEf");
}

int indexNamed(const Plugin& plugin, std::string_view name)
{
    const auto& list = plugin.parameters();

    for (auto i = 0; i < list.size(); ++i)
        if (list[i].name() == name)
            return i;

    return -1;
}

bool near(float a, float b, float tolerance = 1e-4f)
{
    return std::abs(a - b) < tolerance;
}

struct Block
{
    explicit Block(Plugin& pluginToUse, double sampleRate = 48000.0)
        : plugin(pluginToUse)
    {
        auto layout = plugin.getBusLayout();
        context.prepare(layout);

        for (auto i = 0; i < layout.inputs.size(); ++i)
            if (layout.inputs[i].isMain)
                context.inputs[i].setSize(layout.inputs[i].numChannels, blockSize);

        for (auto i = 0; i < layout.outputs.size(); ++i)
            context.outputs[i].setSize(layout.outputs[i].numChannels, blockSize);

        plugin.prepare(
            {.sampleRate = sampleRate, .maxBlockSize = blockSize, .layout = layout});
    }

    void fillInput(float value)
    {
        for (auto channel: context.inputs[0])
            for (auto& sample: channel)
                sample = value;
    }

    void run() { plugin.process(context); }

    Plugin& plugin;
    ProcessContext context;
};

struct EditRecorder : HostEditListener
{
    void beginParameterEdit(int index) noexcept override
    {
        events.add("begin " + std::to_string(index));
    }

    void performParameterEdit(int index, float) noexcept override
    {
        events.add("perform " + std::to_string(index));
    }

    void endParameterEdit(int index) noexcept override
    {
        events.add("end " + std::to_string(index));
    }

    Vector<std::string> events;
};

void notifyGesture(HostedAudioUnit& unit, int index, AudioUnitEventType type)
{
    auto event = AudioUnitEvent();
    event.mEventType = type;
    event.mArgument.mParameter.mAudioUnit = unit.audioUnit();
    event.mArgument.mParameter.mParameterID = unit.parameters().entry(index).hostId;
    event.mArgument.mParameter.mScope = kAudioUnitScope_Global;
    event.mArgument.mParameter.mElement = 0;
    AUEventListenerNotify(nullptr, nullptr, &event);
}

auto tLoad = test("HostedAU/aRegisteredComponentLoads") = []
{
    auto unit = loadTestUnit("Gain");

    check(unit->name() == "Gain");
    check(unit->description().format == PluginFormat::AU);
    check(looksLikeAudioUnitIdentifier(unit->description().classId));
    check(unit->description().path == unit->description().classId);
    check(!unit->wantsHostBypass());
    check(unit->audioUnit() != nullptr);
    check(!unit->isInitialized());
};

auto tBuses = test("HostedAU/theBusesAreTheUnitsElements") = []
{
    auto unit = loadTestUnit("Gain");
    const auto layout = unit->getBusLayout();

    check(!layout.inputs.empty() && layout.inputs[0].isMain);
    check(layout.inputs[0].numChannels == 2);
    check(layout.outputs.size() == 1 && layout.outputs[0].numChannels == 2);
    check(layout.midiInputs.empty());
    check(unit->acceptsLayout(layout));

    auto synth = loadTestUnit("Echo");
    check(synth->getBusLayout().inputs.empty());
    check(synth->getBusLayout().midiInputs.size() == 1);
};

auto tParameters = test("HostedAU/parametersCarryTheUnitsIds") = []
{
    auto unit = loadTestUnit("Gain");
    const auto& list = unit->parameters();

    check(list.size() >= 4);
    check(list.identityProblems().empty());

    for (auto i = 0; i < list.size(); ++i)
    {
        check(list.entry(i).id == std::to_string(list.entry(i).hostId));
        check(unit->indexOfParameterId(list.entry(i).hostId) == i);
    }

    // An indexed parameter's value strings are its text.
    auto mode = indexNamed(*unit, "Mode");
    check(mode >= 0);
    check(list[mode].numSteps() == 2);
    check(list[mode].valueToText(list[mode].minValue() + 1.f) == "Warm");
    check(near(list[mode].textToValue("hot"), list[mode].minValue() + 2.f));

    auto gain = indexNamed(*unit, "Gain");
    check(gain >= 0);

    // The unit's own plain range.
    check(near(list[gain].minValue(), -60.f));
    check(near(list[gain].maxValue(), 12.f));
};

auto tHostEdit = test("HostedAU/aHostEditReachesTheUnit") = []
{
    auto unit = loadTestUnit("Gain");
    auto gain = indexNamed(*unit, "Gain");
    auto id = unit->parameters().entry(gain).hostId;

    unit->parameters()[gain].setValue(-6.f);
    check(near(unit->parameters()[gain].getValue(), -6.f));

    auto value = AudioUnitParameterValue {};
    AudioUnitGetParameter(unit->audioUnit(), id, kAudioUnitScope_Global, 0, &value);
    check(near(value, -6.f, 1e-3f));
};

auto tBehindTheBack = test("HostedAU/aValueTheUnitMovedSilentlyIsReRead") = []
{
    auto unit = loadTestUnit("Gain");
    auto gain = indexNamed(*unit, "Gain");
    auto id = unit->parameters().entry(gain).hostId;

    // What an AU editor may do: set the unit and tell nobody.
    AudioUnitSetParameter(
        unit->audioUnit(), id, kAudioUnitScope_Global, 0, -12.f, 0);
    check(!near(unit->parameters()[gain].getValue(), -12.f));

    unit->refreshParameterValue(gain);
    check(near(unit->parameters()[gain].getValue(), -12.f, 1e-3f));
};

auto tGestures = test("HostedAU/theUnitsOwnGesturesReachTheListener") = []
{
    auto unit = loadTestUnit("Gain");
    auto listener = EditRecorder();
    unit->setHostEditListener(&listener);

    auto gain = indexNamed(*unit, "Gain");
    notifyGesture(*unit, gain, kAudioUnitEvent_BeginParameterChangeGesture);
    notifyGesture(*unit, gain, kAudioUnitEvent_EndParameterChangeGesture);

    eacp::Threads::runEventLoopUntil([&] { return listener.events.size() >= 2; },
                                     eacp::Time::MS {2000});

    check(listener.events.size() == 2);
    check(listener.events.size() == 2
          && listener.events[0] == "begin " + std::to_string(gain));
    check(listener.events.size() == 2
          && listener.events[1] == "end " + std::to_string(gain));

    unit->setHostEditListener(nullptr);
};

auto tAudio = test("HostedAU/aBlockRendersThroughTheUnit") = []
{
    auto unit = loadTestUnit("Gain");
    auto block = Block(*unit);
    check(unit->isInitialized());

    block.fillInput(0.5f);
    block.run();
    check(near(block.context.outputs[0][0][0], 0.5f));

    // From the audio thread, scheduled into the next render: -6 dB.
    auto gain = indexNamed(*unit, "Gain");
    const auto& param = unit->parameters()[gain];
    check(unit->setParameterFromAudioThread(gain, param.toNormalized(-6.0206f)));
    check(!unit->setParameterFromAudioThread(-1, 0.f));

    block.run();
    check(near(block.context.outputs[0][1][blockSize - 1], 0.25f, 1e-3f));

    unit->syncEditorWithProcessor();

    // Longer than prepared: refused, not overrun.
    block.context.outputs[0].setSize(2, blockSize * 2);
    block.run();
};

auto tPlayhead = test("HostedAU/theFallbackPlayheadReachesTheUnit") = []
{
    auto unit = loadTestUnit("Gain");
    auto block = Block(*unit);
    auto* gain =
        static_cast<TestPlugins::GainPlugin*>(TestPlugins::Module::lastCreated);

    block.run();
    check(gain->playhead.isValid);
    check(!gain->playhead.isPlaying);
    check(near(static_cast<float>(gain->playhead.bpm), 120.f));

    auto fallback = Playhead();
    fallback.isPlaying = true;
    fallback.bpm = 140.0;
    unit->setFallbackPlayhead(fallback);

    block.run();
    check(gain->playhead.isPlaying);
    check(near(static_cast<float>(gain->playhead.bpm), 140.f));
};

auto tMidi = test("HostedAU/midiReachesAnInstrument") = []
{
    auto unit = loadTestUnit("Echo");
    auto block = Block(*unit);
    auto* synth =
        static_cast<TestPlugins::SynthPlugin*>(TestPlugins::Module::lastCreated);

    block.context.midiIn[0].add(MIDI::Event::noteOn(2, 64, 1.f, 5));
    block.context.midiIn[0].add(MIDI::Event::controlChange(2, 7, 0.5f, 9));
    block.run();

    check(synth->numEvents == 2);
    check(synth->events[0].asNoteOn() != nullptr);
    check(synth->events[0].asNoteOn()->pitch == 64);
    check(synth->events[0].channel == 2);
    check(synth->events[0].sampleOffset == 5);
    check(synth->events[1].asControlChange() != nullptr);
    check(near(block.context.outputs[0][0][0], 0.5f));
};

auto tMidiOut = test("HostedAU/midiGoesInAndComesBackOut") = []
{
    auto plugin =
        loadFixtureUnit("HostFixtureInstrument", kAudioUnitType_MusicDevice, "HfIn");

    if (plugin == nullptr)
        return;

    const auto layout = plugin->getBusLayout();
    check(layout.inputs.empty());
    check(layout.midiInputs.size() == 1);
    check(layout.midiOutputs.size() == 1);

    auto block = Block(*plugin);
    block.run();
    check(block.context.outputs[0][0][0] == 0.f);
    check(block.context.midiOut[0].empty());

    block.context.midiIn[0].add(MIDI::Event::noteOn(3, 60, 0.5f, 7));
    block.run();
    block.context.midiIn[0].clear();

    check(near(block.context.outputs[0][0][0], 0.8f));
    check(block.context.midiOut[0].size() == 1);

    if (block.context.midiOut[0].size() == 1)
    {
        const auto& echoed = block.context.midiOut[0][0];
        check(echoed.sampleOffset == 7);
        check(echoed.channel == 3);
        check(echoed.asNoteOn() != nullptr && echoed.asNoteOn()->pitch == 60);
        check(near(echoed.asNoteOn()->velocity, 0.5f, 0.01f));
    }

    block.context.midiOut[0].clear();
    block.context.midiIn[0].add(MIDI::Event::noteOff(3, 60, 0.f, 0));
    block.context.midiIn[0].add(MIDI::Event::controlChange(3, 74, 0.5f, 12));
    block.run();

    check(block.context.outputs[0][0][blockSize - 1] == 0.f);
    check(block.context.midiOut[0].size() == 2);
    check(block.context.midiOut[0].size() == 2
          && block.context.midiOut[0][0].isNoteOff());
    check(block.context.midiOut[0].size() == 2
          && block.context.midiOut[0][1].isControlChange()
          && block.context.midiOut[0][1].sampleOffset == 12);
};

auto tState = test("HostedAU/theStateRoundTripsThroughAFreshUnit") = []
{
    auto source = loadTestUnit("Gain");
    auto gain = indexNamed(*source, "Gain");
    auto mode = indexNamed(*source, "Mode");

    source->parameters()[gain].setValue(-3.f);
    source->parameters()[mode].setValue(source->parameters()[mode].minValue() + 2.f);

    auto state = source->saveState(StateContext::Session);
    check(!state.empty());

    auto copy = loadTestUnit("Gain");
    copy->loadState(state, StateContext::Session);

    check(near(copy->parameters()[gain].getValue(), -3.f, 1e-3f));
    check(copy->parameters()[mode].valueToText(copy->parameters()[mode].getValue())
          == "Hot");
};

auto tFixture = test("HostedAU/aFixtureComponentLoadsAndRuns") = []
{
    auto plugin = loadFixtureEffect();

    if (plugin == nullptr)
        return;

    check(plugin->getBusLayout().inputs.size() == 2);
    check(indexNamed(*plugin, "Cutoff") >= 0);

    auto block = Block(*plugin);
    block.fillInput(0.5f);
    block.run();
    check(near(block.context.outputs[0][0][0], 0.5f));

    // A value the unit wrote itself: an AU has no output-parameter channel, so a
    // host re-reads it. The fixture's adapter publishes a value its plugin wrote
    // at the next render.
    block.run();
    auto peak = indexNamed(*plugin, "Peak");
    plugin->refreshParameterValue(peak);
    check(near(plugin->parameters()[peak].getValue(), 0.5f));

    auto instrument =
        loadFixtureUnit("HostFixtureInstrument", kAudioUnitType_MusicDevice, "HfIn");
    check(instrument != nullptr
          && instrument->getBusLayout().midiInputs.size() == 1);
};

auto tFractionalRate = test("HostedAU/aFractionalRateArrivesUnrounded") = []
{
    auto plugin = loadFixtureEffect();

    if (plugin == nullptr)
        return;

    auto fraction = indexNamed(*plugin, "Rate Fraction");
    const auto& list = plugin->parameters();

    // Through the stream formats and the fixture's own AU adapter, whole.
    auto block = Block(*plugin, 44055.94);
    block.run();
    block.run();
    plugin->refreshParameterValue(fraction);
    check(near(list[fraction].getValue(), 0.94f, 1e-3f));

    auto whole = Block(*plugin, 48000.0);
    whole.run();
    whole.run();
    plugin->refreshParameterValue(fraction);
    check(list[fraction].getValue() == 0.f);
};

auto tAppleDelay = test("HostedAU/applesDelayLoadsThroughTheFormats") = []
{
    const auto formats = DefaultFormats();
    auto scan = formats.scanTarget("aufx:dely:appl").scan;
    check(scan.classes.size() == 1);

    if (scan.classes.empty())
        return;

    auto loaded = formats.load(scan.classes[0]);
    check(loaded.error.empty());
    check(loaded.plugin != nullptr);

    if (loaded.plugin == nullptr)
        return;

    check(loaded.plugin->name() == "AUDelay");
    check(loaded.plugin->parameters().size() > 0);
    check(indexNamed(*loaded.plugin, "Dry/Wet Mix") >= 0
          || indexNamed(*loaded.plugin, "dry/wet mix") >= 0
          || loaded.plugin->parameters().size() >= 3);

    auto block = Block(*loaded.plugin);
    block.fillInput(0.25f);
    block.run();

    auto missing = scan.classes[0];
    missing.classId = "aufx:Nope:MkTs";
    check(formats.load(missing).plugin == nullptr);
};
} // namespace
} // namespace HostedAUTests
