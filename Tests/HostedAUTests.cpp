// Hosting Audio Units: the test module's own AU components, registered in this
// process and handed to the loader directly (never found through the registry),
// and Apple's AUDelay found and loaded the way a host loads a scanned plugin.
// Buses, parameters (ids, value strings, text), a host edit and that it does not
// echo back, the unit's own gestures reaching the listener, a value moved behind
// the host's back, audio-thread values reaching the unit's other listeners,
// renames and a changed parameter set, a list published only once initialized,
// latency and tail, audio, MIDI in, the state round trip and the fallback
// playhead. What the fixtures cannot do, a unit made by hand below does.

#include "AUTestHost.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/AU/AUFormat.h>
#include <MakeASound/Hosting/AU/HostedAudioUnit.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <dlfcn.h>
#include <filesystem>
#include <map>
#include <string>
#include <thread>

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

    void parameterInfoChanged() noexcept override { ++infos; }
    void latencyChanged() noexcept override { ++latencies; }

    Vector<std::string> events;
    int infos = 0;
    int latencies = 0;
};

// Lets the main run loop deliver what is pending, for a check that nothing comes.
void turnTheRunLoop(eacp::Time::MS duration = eacp::Time::MS {250})
{
    eacp::Threads::runEventLoopUntil([] { return false; }, duration);
}

// Another listener on one of the unit's parameters, on the main run loop, as the
// unit's own view would be: what it hears and the last value it heard.
struct ParameterObserver
{
    ParameterObserver(AudioUnit unit, AudioUnitParameterID id)
        : parameter {unit, id, kAudioUnitScope_Global, 0}
    {
        AUListenerCreate(
            &heard, this, CFRunLoopGetMain(), kCFRunLoopDefaultMode, 0.f, &listener);
        check(listener != nullptr);

        if (listener != nullptr)
            AUListenerAddParameter(listener, nullptr, &parameter);
    }

    ParameterObserver(const ParameterObserver&) = delete;
    ParameterObserver& operator=(const ParameterObserver&) = delete;

    ~ParameterObserver()
    {
        if (listener != nullptr)
            AUListenerDispose(listener);
    }

    static void heard(void* self,
                      void*,
                      const AudioUnitParameter*,
                      AudioUnitParameterValue value)
    {
        auto& observer = *static_cast<ParameterObserver*>(self);
        ++observer.notifications;
        observer.lastValue = value;
    }

    AudioUnitParameter parameter;
    AUParameterListenerRef listener = nullptr;
    int notifications = 0;
    float lastValue = 0.f;
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

// An Audio Unit made by hand over the component C API, for what the fixtures
// cannot show (MakeASound's adapter lists its parameters from the start, never
// changes the set, and renames through the list and the info together): a list
// published only once initialized, a set that grows or shrinks while loaded, a
// rename told through ParameterInfo alone, and a latency told from another
// thread. Registered in this process, its component handed to the loader.
namespace Stub
{
constexpr auto numParameters = 3;

struct PropertyListener
{
    AudioUnitPropertyID property = 0;
    AudioUnitPropertyListenerProc proc = nullptr;
    void* userData = nullptr;
};

struct Unit
{
    AudioComponentInstance instance = nullptr;
    bool initialized = false;
    int initializeCalls = 0;
    std::array<AudioUnitParameterValue, numParameters> values {};

    // What ParameterInfo says: an empty name is "Param <id>", anything else goes
    // out as a CFString the host releases.
    std::array<std::string, numParameters> names {};
    std::array<AudioUnitParameterUnit, numParameters> units {};

    // As some units do: no parameter list until initialized.
    bool listOnlyOnceInitialized = false;

    // Parameters listed beyond the first ones, or (negative) fewer.
    int extraParameters = 0;

    // As some units do: more parameters once initialized than when loaded.
    int extraOnceInitialized = 0;

    Float64 latencySeconds = 0.0;

    // As many units do: tell the latency on every initialize, moved or not.
    bool announceLatencyOnInitialize = false;

    Vector<PropertyListener> listeners;
};

// What the factory hands the system: the interface first, so the pointer every
// method gets back is this.
struct Instance
{
    AudioComponentPlugInInterface plugIn {};
    Unit* unit = nullptr;
};

// What the factory starts each unit from, and the unit it opened last.
Unit nextUnit;
Unit* lastOpened = nullptr;

Unit& self(void* ref)
{
    return *static_cast<Instance*>(ref)->unit;
}

UInt32 listedCount(const Unit& unit)
{
    if (unit.listOnlyOnceInitialized && !unit.initialized)
        return 0;

    auto count = numParameters + unit.extraParameters
                 + (unit.initialized ? unit.extraOnceInitialized : 0);
    return static_cast<UInt32>(std::max(0, count));
}

// What a unit's PropertyChanged does: every listener for the property hears it.
void notify(Unit& unit, AudioUnitPropertyID property, AudioUnitElement element = 0)
{
    for (const auto& listener: unit.listeners)
        if (listener.property == property)
            listener.proc(listener.userData,
                          unit.instance,
                          property,
                          kAudioUnitScope_Global,
                          element);
}

OSStatus open(void* ref, AudioComponentInstance instance)
{
    self(ref).instance = instance;
    lastOpened = &self(ref);
    return noErr;
}

OSStatus close(void* ref)
{
    auto* instance = static_cast<Instance*>(ref);

    if (lastOpened == instance->unit)
        lastOpened = nullptr;

    delete instance->unit;
    delete instance;
    return noErr;
}

OSStatus initialize(void* ref)
{
    auto& unit = self(ref);
    unit.initialized = true;
    ++unit.initializeCalls;

    // What a unit's Initialize often tells: its list and its latency.
    notify(unit, kAudioUnitProperty_ParameterList, 0);

    if (unit.announceLatencyOnInitialize)
        notify(unit, kAudioUnitProperty_Latency, 0);

    return noErr;
}

OSStatus uninitialize(void* ref)
{
    self(ref).initialized = false;
    return noErr;
}

OSStatus getPropertyInfo(void* ref,
                         AudioUnitPropertyID property,
                         AudioUnitScope scope,
                         AudioUnitElement,
                         UInt32* size,
                         Boolean* writable)
{
    if (writable != nullptr)
        *writable = false;

    if (scope != kAudioUnitScope_Global)
        return kAudioUnitErr_InvalidScope;

    auto bytes = UInt32 {0};

    switch (property)
    {
        case kAudioUnitProperty_ParameterList:
            bytes = listedCount(self(ref)) * sizeof(AudioUnitParameterID);
            break;
        case kAudioUnitProperty_ParameterInfo:
            bytes = sizeof(AudioUnitParameterInfo);
            break;
        case kAudioUnitProperty_Latency:
            bytes = sizeof(Float64);
            break;
        default:
            return kAudioUnitErr_InvalidProperty;
    }

    if (size != nullptr)
        *size = bytes;

    return noErr;
}

OSStatus getProperty(void* ref,
                     AudioUnitPropertyID property,
                     AudioUnitScope scope,
                     AudioUnitElement element,
                     void* data,
                     UInt32* size)
{
    if (scope != kAudioUnitScope_Global)
        return kAudioUnitErr_InvalidScope;

    auto& unit = self(ref);

    if (property == kAudioUnitProperty_ParameterList)
    {
        auto count = std::min<UInt32>(listedCount(unit),
                                      *size / sizeof(AudioUnitParameterID));
        auto* ids = static_cast<AudioUnitParameterID*>(data);

        for (auto id = UInt32 {0}; id < count; ++id)
            ids[id] = id;

        *size = count * sizeof(AudioUnitParameterID);
        return noErr;
    }

    if (property == kAudioUnitProperty_ParameterInfo)
    {
        if (element >= static_cast<UInt32>(numParameters) + 2)
            return kAudioUnitErr_InvalidParameter;

        auto& info = *static_cast<AudioUnitParameterInfo*>(data);
        info = AudioUnitParameterInfo();
        std::snprintf(info.name, sizeof(info.name), "Param %u", element);
        info.unit = kAudioUnitParameterUnit_Generic;
        info.minValue = 0.f;
        info.maxValue = 1.f;
        info.defaultValue = 0.5f;
        info.flags =
            kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable;

        if (element < static_cast<UInt32>(numParameters))
        {
            info.unit = unit.units[element];

            if (const auto& name = unit.names[element]; !name.empty())
            {
                info.cfNameString = CFStringCreateWithCString(
                    kCFAllocatorDefault, name.c_str(), kCFStringEncodingUTF8);
                info.flags |= kAudioUnitParameterFlag_HasCFNameString
                              | kAudioUnitParameterFlag_CFNameRelease;
            }
        }

        *size = sizeof(info);
        return noErr;
    }

    if (property == kAudioUnitProperty_Latency)
    {
        *static_cast<Float64*>(data) = unit.latencySeconds;
        *size = sizeof(Float64);
        return noErr;
    }

    return kAudioUnitErr_InvalidProperty;
}

OSStatus getParameter(void* ref,
                      AudioUnitParameterID id,
                      AudioUnitScope,
                      AudioUnitElement,
                      AudioUnitParameterValue* value)
{
    if (id >= static_cast<UInt32>(numParameters))
        return kAudioUnitErr_InvalidParameter;

    *value = self(ref).values[id];
    return noErr;
}

OSStatus setParameter(void* ref,
                      AudioUnitParameterID id,
                      AudioUnitScope,
                      AudioUnitElement,
                      AudioUnitParameterValue value,
                      UInt32)
{
    if (id >= static_cast<UInt32>(numParameters))
        return kAudioUnitErr_InvalidParameter;

    self(ref).values[id] = value;
    return noErr;
}

OSStatus addPropertyListener(void* ref,
                             AudioUnitPropertyID property,
                             AudioUnitPropertyListenerProc proc,
                             void* userData)
{
    self(ref).listeners.add({property, proc, userData});
    return noErr;
}

OSStatus removePropertyListener(void* ref,
                                AudioUnitPropertyID property,
                                AudioUnitPropertyListenerProc proc,
                                void* userData)
{
    auto kept = Vector<PropertyListener>();

    for (const auto& listener: self(ref).listeners)
        if (listener.property != property || listener.proc != proc
            || listener.userData != userData)
            kept.add(listener);

    self(ref).listeners = std::move(kept);
    return noErr;
}

AudioComponentMethod lookup(SInt16 selector)
{
    auto method = [](auto function)
    { return reinterpret_cast<AudioComponentMethod>(function); };

    switch (selector)
    {
        case kAudioUnitInitializeSelect:
            return method(&initialize);
        case kAudioUnitUninitializeSelect:
            return method(&uninitialize);
        case kAudioUnitGetPropertyInfoSelect:
            return method(&getPropertyInfo);
        case kAudioUnitGetPropertySelect:
            return method(&getProperty);
        case kAudioUnitGetParameterSelect:
            return method(&getParameter);
        case kAudioUnitSetParameterSelect:
            return method(&setParameter);
        case kAudioUnitAddPropertyListenerSelect:
            return method(&addPropertyListener);
        case kAudioUnitRemovePropertyListenerWithUserDataSelect:
            return method(&removePropertyListener);
        default:
            return nullptr;
    }
}

AudioComponentPlugInInterface* factory(const AudioComponentDescription*)
{
    auto* instance = new Instance();
    instance->unit = new Unit(nextUnit);
    instance->plugIn.Open = &open;
    instance->plugIn.Close = &close;
    instance->plugIn.Lookup = &lookup;
    return &instance->plugIn;
}

AudioComponent component()
{
    static const auto registered = []
    {
        auto description = AudioComponentDescription {
            .componentType = kAudioUnitType_MusicDevice,
            .componentSubType = FourCC("HsSt").toUint32(),
            .componentManufacturer = FourCC("MkTs").toUint32()};

        return AudioComponentRegister(
            &description, CFSTR("MakeASound Tests: Hand-Made Unit"), 1, &factory);
    }();

    check(registered != nullptr);
    return registered;
}

// The hand-made unit, loaded as a host would, with what it was set up to do
// before the host first reads it.
struct Loaded
{
    OwningPointer<HostedAudioUnit> plugin;
    Unit* unit = nullptr;

    explicit operator bool() const { return plugin != nullptr && unit != nullptr; }

    HostedAudioUnitParameter& parameter(int index) const
    {
        return static_cast<HostedAudioUnitParameter&>(plugin->parameters()[index]);
    }
};

Loaded load(const Unit& settings = {})
{
    auto loaded = Loaded();
    auto* registered = component();

    if (registered == nullptr)
        return loaded;

    // The factory starts the next unit from these.
    nextUnit = settings;
    lastOpened = nullptr;

    auto description = Hosting::PluginDescription();
    description.name = "Hand-Made Unit";

    auto result = loadAudioUnit(registered, description, {});
    nextUnit = Unit();
    check(result.error.empty());

    auto* unit = dynamic_cast<HostedAudioUnit*>(result.plugin.get());
    check(unit != nullptr && lastOpened != nullptr);

    if (unit == nullptr || lastOpened == nullptr)
        return loaded;

    result.plugin.release();
    loaded.plugin = OwningPointer<HostedAudioUnit>(unit);
    loaded.unit = lastOpened;
    return loaded;
}
} // namespace Stub

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

    // Every parameter readable or writable and no clumps out of order: JUCE's
    // host listed them in the same order, so a JUCE index is the index here.
    for (auto i = 0; i < list.size(); ++i)
        check(unit->indexForControllerIndex(i) == i);

    check(unit->indexForControllerIndex(list.size()) == -1);
    check(unit->indexForControllerIndex(-1) == -1);
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

    // The ClassInfo is all of an Audio Unit's state: no controller state.
    check(source->saveControllerState().empty());
    copy->loadControllerState("anything");
    check(near(copy->parameters()[gain].getValue(), -3.f, 1e-3f));
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

auto tTail = test("HostedAU/aTailIsReadFiniteOrInfinite") = []
{
    auto effect = loadFixtureEffect();
    auto instrument =
        loadFixtureUnit("HostFixtureInstrument", kAudioUnitType_MusicDevice, "HfIn");

    if (effect == nullptr || instrument == nullptr)
        return;

    // The tail time is in seconds, read back at the prepared rate.
    auto block = Block(*effect);
    check(effect->tailSamples() == 4800);

    // An infinite tail time, before and after prepare().
    check(instrument->tailSamples() == Plugin::infiniteTail);
    auto instrumentBlock = Block(*instrument);
    check(instrument->tailSamples() == Plugin::infiniteTail);
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

    // Apple's delay lists plain parameters: JUCE's indices are these.
    for (auto i = 0; i < loaded.plugin->parameters().size(); ++i)
        check(loaded.plugin->indexForControllerIndex(i) == i);

    auto block = Block(*loaded.plugin);
    block.fillInput(0.25f);
    block.run();

    auto missing = scan.classes[0];
    missing.classId = "aufx:Nope:MkTs";
    check(formats.load(missing).plugin == nullptr);
};

auto tNoEcho = test("HostedAU/aHostEditIsNotEchoedBackAsTheUnits") = []
{
    auto unit = loadTestUnit("Gain");
    auto gain = indexNamed(*unit, "Gain");
    auto observer =
        ParameterObserver(unit->audioUnit(), unit->parameters().entry(gain).hostId);
    auto listener = EditRecorder();
    unit->setHostEditListener(&listener);

    unit->parameters()[gain].setValue(-6.f);

    // The unit's other listeners (its own view) hear the host's edit...
    eacp::Threads::runEventLoopUntil([&] { return observer.notifications > 0; },
                                     eacp::Time::MS {2000});
    turnTheRunLoop();
    check(observer.notifications >= 1);
    check(near(observer.lastValue, -6.f, 1e-3f));

    // ...and the host does not hear it back as one the unit made, which would
    // loop through a touch guard.
    check(listener.events.empty());

    unit->setHostEditListener(nullptr);
};

auto tScheduled = test("HostedAU/audioThreadValuesReachTheUnitsListenersOnSync") = []
{
    auto unit = loadTestUnit("Gain");
    auto gain = indexNamed(*unit, "Gain");
    auto observer =
        ParameterObserver(unit->audioUnit(), unit->parameters().entry(gain).hostId);
    auto listener = EditRecorder();
    unit->setHostEditListener(&listener);

    auto block = Block(*unit);
    turnTheRunLoop();
    observer.notifications = 0;

    // Nothing scheduled: nothing to tell.
    unit->syncEditorWithProcessor();
    turnTheRunLoop();
    check(observer.notifications == 0);

    const auto& param = unit->parameters()[gain];
    check(unit->setParameterFromAudioThread(gain, param.toNormalized(-6.0206f)));
    block.run();

    // The render itself tells nobody: the audio thread cannot.
    turnTheRunLoop();
    check(observer.notifications == 0);

    // The sync does, with the value the unit rendered.
    unit->syncEditorWithProcessor();
    eacp::Threads::runEventLoopUntil([&] { return observer.notifications > 0; },
                                     eacp::Time::MS {2000});
    check(observer.notifications == 1);
    check(near(observer.lastValue, -6.0206f, 1e-3f));

    // Once: a sync with nothing newly scheduled tells nobody again.
    unit->syncEditorWithProcessor();
    turnTheRunLoop();
    check(observer.notifications == 1);

    // And the host's listener never hears it back as the unit's own edit.
    check(listener.events.empty());

    unit->setHostEditListener(nullptr);
};

auto tRename = test("HostedAU/aRenameIsReadBackInPlaceOnceTheRunLoopTurns") = []
{
    auto plugin = loadFixtureEffect();

    if (plugin == nullptr)
        return;

    auto* unit = dynamic_cast<HostedAudioUnit*>(plugin.get());
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);

    auto cutoff = indexNamed(*plugin, "Cutoff");
    const auto entry = plugin->parameters().entry(cutoff);
    auto alt = indexNamed(*plugin, "Alt Names");

    plugin->parameters()[alt].setValue(1.f);
    check(!plugin->takeParameterInfoChanged());

    // The fixture renames when a state with Alt Names on is loaded, and its
    // adapter tells the unit's property listeners at once.
    plugin->loadState(plugin->saveState(StateContext::Session),
                      StateContext::Session);

    // The names are read back on the main run loop, not in the property
    // listener (which may run on any thread): until it turns, nothing moved.
    check(!plugin->takeParameterInfoChanged());
    check(plugin->parameters()[cutoff].name() == "Cutoff");
    check(listener.infos == 0);

    eacp::Threads::runEventLoopUntil([&] { return listener.infos > 0; },
                                     eacp::Time::MS {2000});

    check(listener.infos == 1);
    check(plugin->takeParameterInfoChanged());
    check(!plugin->takeParameterInfoChanged());
    check(!plugin->takeIoChanged());
    check(plugin->parameters()[cutoff].name() == "Frequency");
    check(plugin->parameters().entry(cutoff).displayName == "Frequency");
    check(plugin->parameters().entry(cutoff).id == entry.id);
    check(plugin->parameters().entry(cutoff).hostId == entry.hostId);
    check(indexNamed(*plugin, "Frequency") == cutoff);

    // The renamed parameter still resolves, by id and in the unit's gestures.
    check(unit->indexOfParameterId(entry.hostId) == cutoff);

    // (The values the load posted arrive first.)
    turnTheRunLoop();
    listener.events.clear();
    notifyGesture(*unit, cutoff, kAudioUnitEvent_BeginParameterChangeGesture);
    eacp::Threads::runEventLoopUntil([&] { return !listener.events.empty(); },
                                     eacp::Time::MS {2000});
    check(listener.events.size() == 1
          && listener.events[0] == "begin " + std::to_string(cutoff));

    plugin->setHostEditListener(nullptr);
};

auto tInfoRename = test("HostedAU/aParameterInfoChangeRenamesInPlace") = []
{
    auto loaded = Stub::load();

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    check(plugin.parameters().size() == Stub::numParameters);

    auto& renamed = loaded.parameter(1);
    auto& untouched = loaded.parameter(0);
    const auto id = plugin.parameters().entry(1).id;

    check(renamed.name() == "Param 1");
    check(renamed.label().empty());

    auto listener = EditRecorder();
    plugin.setHostEditListener(&listener);

    // A rename told through ParameterInfo alone, as many units do.
    loaded.unit->names[1] = "Cutoff";
    loaded.unit->units[1] = kAudioUnitParameterUnit_Hertz;
    Stub::notify(
        *loaded.unit, kAudioUnitProperty_ParameterInfo, renamed.parameterId());

    // Read back on the main run loop, never in the property listener.
    check(!plugin.takeParameterInfoChanged());
    check(renamed.name() == "Param 1");

    eacp::Threads::runEventLoopUntil([&] { return listener.infos > 0; },
                                     eacp::Time::MS {2000});

    check(listener.infos == 1);
    check(plugin.takeParameterInfoChanged());
    check(!plugin.takeParameterInfoChanged());
    check(!plugin.takeIoChanged());

    // In place: the same parameter, its ids unmoved, with the new name and unit.
    check(plugin.parameters().size() == Stub::numParameters);
    check(&plugin.parameters()[1] == &renamed);
    check(renamed.name() == "Cutoff");
    check(renamed.label() == "Hz");
    check(renamed.parameterId() == 1);
    check(plugin.parameters().entry(1).displayName == "Cutoff");
    check(plugin.parameters().entry(1).id == id);
    check(untouched.name() == "Param 0");

    // The id lookups behind the event listener still find it at its index.
    check(plugin.indexOfParameterId(1) == 1);
    notifyGesture(plugin, 1, kAudioUnitEvent_BeginParameterChangeGesture);
    eacp::Threads::runEventLoopUntil([&] { return !listener.events.empty(); },
                                     eacp::Time::MS {2000});
    check(listener.events.size() == 1 && listener.events[0] == "begin 1");

    // Told again with nothing renamed: nobody hears of it.
    Stub::notify(*loaded.unit, kAudioUnitProperty_ParameterInfo, 1);
    turnTheRunLoop();
    check(listener.infos == 1);
    check(!plugin.takeParameterInfoChanged());

    plugin.setHostEditListener(nullptr);
};

auto tListRename = test("HostedAU/aParameterListWithTheSameIdsRefreshesNames") = []
{
    auto loaded = Stub::load();

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    auto listener = EditRecorder();
    plugin.setHostEditListener(&listener);

    loaded.unit->names[0] = "Drive";
    Stub::notify(*loaded.unit, kAudioUnitProperty_ParameterList);

    eacp::Threads::runEventLoopUntil([&] { return listener.infos > 0; },
                                     eacp::Time::MS {2000});

    check(listener.infos == 1);
    check(plugin.takeParameterInfoChanged());
    check(loaded.parameter(0).name() == "Drive");
    check(plugin.parameters().size() == Stub::numParameters);

    // The same ids: names re-read in place, no reload asked for.
    check(!plugin.takeIoChanged());

    plugin.setHostEditListener(nullptr);
};

auto tChangedSet = test("HostedAU/aParameterSetThatChangedIsAnIoChange") = []
{
    auto loaded = Stub::load();

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    auto& first = loaded.parameter(0);
    auto listener = EditRecorder();
    plugin.setHostEditListener(&listener);

    // A parameter more, and a rename alongside it.
    loaded.unit->extraParameters = 1;
    loaded.unit->names[0] = "Renamed Alongside";
    Stub::notify(*loaded.unit, kAudioUnitProperty_ParameterList);
    turnTheRunLoop();

    // Swapping parameters in place would move indices a host holds, so a set
    // that changed is the host's to reload: an I/O change, not a rename, and the
    // table stays as it was, objects and names alike.
    check(plugin.takeIoChanged());
    check(!plugin.takeIoChanged());
    check(!plugin.takeParameterInfoChanged());
    check(listener.infos == 0);
    check(plugin.parameters().size() == Stub::numParameters);
    check(&plugin.parameters()[0] == &first);
    check(first.name() == "Param 0");

    // A parameter fewer than it had: the same.
    loaded.unit->extraParameters = -1;
    Stub::notify(*loaded.unit, kAudioUnitProperty_ParameterList);
    turnTheRunLoop();

    check(plugin.takeIoChanged());
    check(!plugin.takeParameterInfoChanged());
    check(listener.infos == 0);
    check(plugin.parameters().size() == Stub::numParameters);
    check(first.name() == "Param 0");

    plugin.setHostEditListener(nullptr);
};

auto tLateList = test("HostedAU/aListPublishedOnlyOnceInitializedIsReadAtLoad") = []
{
    auto settings = Stub::Unit();
    settings.listOnlyOnceInitialized = true;
    auto loaded = Stub::load(settings);

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;

    // Initialized once to read the list, and handed over uninitialized.
    check(plugin.parameters().size() == Stub::numParameters);
    check(!plugin.isInitialized());
    check(!loaded.unit->initialized);
    check(loaded.unit->initializeCalls == 1);
    check(loaded.parameter(2).name() == "Param 2");

    // The event listener covers the parameters read that way.
    auto listener = EditRecorder();
    plugin.setHostEditListener(&listener);

    notifyGesture(plugin, 1, kAudioUnitEvent_BeginParameterChangeGesture);
    eacp::Threads::runEventLoopUntil([&] { return !listener.events.empty(); },
                                     eacp::Time::MS {2000});
    check(listener.events.size() == 1 && listener.events[0] == "begin 1");

    plugin.setHostEditListener(nullptr);

    // prepare() still initializes it.
    plugin.prepare({.sampleRate = 48000.0,
                    .maxBlockSize = blockSize,
                    .layout = plugin.getBusLayout()});
    check(plugin.isInitialized());
    check(loaded.unit->initialized);
    check(loaded.unit->initializeCalls == 2);
    check(plugin.parameters().size() == Stub::numParameters);
};

auto tLatency = test("HostedAU/aLatencyChangeIsReported") = []
{
    auto plugin = loadFixtureEffect();

    if (plugin == nullptr)
        return;

    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);
    auto latency = indexNamed(*plugin, "Latency");

    // Read at load, in seconds at the unit's rate, before any prepare().
    check(plugin->latencySamples() == 0);
    check(plugin->tailSamples() == 4800);

    auto block = Block(*plugin);
    check(plugin->latencySamples() == 0);
    check(!plugin->takeLatencyChanged());

    // A host edit the unit's latency follows: the unit tells its property
    // listeners, and the host reads the new latency at once.
    plugin->parameters()[latency].setValue(512.f);

    check(plugin->takeLatencyChanged());
    check(!plugin->takeLatencyChanged());
    check(plugin->latencySamples() == 512);

    // The listener hears of it on the main run loop.
    eacp::Threads::runEventLoopUntil([&] { return listener.latencies > 0; },
                                     eacp::Time::MS {2000});
    check(listener.latencies == 1);

    // A fresh unit reads the latency a state brings.
    auto copy = loadFixtureEffect();
    copy->loadState(plugin->saveState(StateContext::Session), StateContext::Session);
    check(copy->latencySamples() == 512);

    plugin->setHostEditListener(nullptr);
};

auto tLatencyThread =
    test("HostedAU/aLatencyToldFromAnotherThreadReachesTheListenerOnTheRunLoop") = []
{
    auto loaded = Stub::load();

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    plugin.prepare({.sampleRate = 48000.0,
                    .maxBlockSize = blockSize,
                    .layout = plugin.getBusLayout()});
    check(plugin.latencySamples() == 0);
    check(!plugin.takeLatencyChanged());

    auto listener = EditRecorder();
    plugin.setHostEditListener(&listener);

    // A unit may tell its property listeners from its render thread.
    loaded.unit->latencySeconds = 0.01;
    std::thread([&] { Stub::notify(*loaded.unit, kAudioUnitProperty_Latency); })
        .join();

    // The flag and the figure at once, from any thread...
    check(plugin.takeLatencyChanged());
    check(plugin.latencySamples() == 480);

    // ...the listener only on the main run loop.
    check(listener.latencies == 0);
    eacp::Threads::runEventLoopUntil([&] { return listener.latencies > 0; },
                                     eacp::Time::MS {2000});
    check(listener.latencies == 1);

    plugin.setHostEditListener(nullptr);
};

void prepareAt48k(HostedAudioUnit& plugin)
{
    plugin.prepare({.sampleRate = 48000.0,
                    .maxBlockSize = blockSize,
                    .layout = plugin.getBusLayout()});
}

auto tSetOnInitialize = test("HostedAU/aSetThatChangesOnInitializeIsNoIoChange") = []
{
    // Lists one parameter more once initialized than when loaded.
    auto settings = Stub::Unit();
    settings.extraOnceInitialized = 1;
    auto loaded = Stub::load(settings);

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    check(plugin.parameters().size() == Stub::numParameters);

    // Initialize tells the listeners about the list: the set the host prepared
    // against, so no reload is asked for, and preparing again ends there.
    prepareAt48k(plugin);
    turnTheRunLoop();
    check(!plugin.takeIoChanged());

    prepareAt48k(plugin);
    turnTheRunLoop();
    check(!plugin.takeIoChanged());

    // A change once prepared is one: told once per change, and a host that only
    // prepares again on it is not asked again.
    loaded.unit->extraParameters = 1;
    Stub::notify(*loaded.unit, kAudioUnitProperty_ParameterList);
    turnTheRunLoop();
    check(plugin.takeIoChanged());

    prepareAt48k(plugin);
    turnTheRunLoop();
    check(!plugin.takeIoChanged());

    // The table is the one it loaded with throughout, so every index holds.
    check(plugin.parameters().size() == Stub::numParameters);
    check(!plugin.setParameterFromAudioThread(Stub::numParameters, 0.5f));
    check(plugin.setParameterFromAudioThread(Stub::numParameters - 1, 0.5f));
};

auto tLatencyUnmoved =
    test("HostedAU/aLatencyToldButUnmovedDoesNotReachTheListener") = []
{
    auto settings = Stub::Unit();
    settings.latencySeconds = 0.01;
    settings.announceLatencyOnInitialize = true;
    auto loaded = Stub::load(settings);

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    prepareAt48k(plugin);
    turnTheRunLoop();
    check(plugin.latencySamples() == 480);

    auto listener = EditRecorder();
    plugin.setHostEditListener(&listener);

    // A unit that tells its latency on every initialize, the figure unmoved: a
    // host that prepares again on latencyChanged() is not told again.
    prepareAt48k(plugin);
    turnTheRunLoop();
    check(listener.latencies == 0);

    // Told without a move: the flag, as ever, but not the listener.
    Stub::notify(*loaded.unit, kAudioUnitProperty_Latency);
    turnTheRunLoop();
    check(plugin.takeLatencyChanged());
    check(listener.latencies == 0);

    // Moved: the listener, once.
    loaded.unit->latencySeconds = 0.02;
    Stub::notify(*loaded.unit, kAudioUnitProperty_Latency);
    turnTheRunLoop();
    check(plugin.latencySamples() == 960);
    check(listener.latencies == 1);

    plugin.setHostEditListener(nullptr);
};

auto tPendingDies = test("HostedAU/whatIsPendingForTheRunLoopDiesWithTheUnit") = []
{
    auto listener = EditRecorder();

    {
        auto loaded = Stub::load();

        if (!loaded)
            return;

        loaded.plugin->setHostEditListener(&listener);

        // Told from another thread, as a render thread would, and the unit gone
        // before the main run loop turns.
        loaded.unit->latencySeconds = 0.01;
        loaded.unit->names[0] = "Renamed";
        std::thread(
            [&]
            {
                Stub::notify(*loaded.unit, kAudioUnitProperty_Latency);
                Stub::notify(*loaded.unit, kAudioUnitProperty_ParameterInfo);
            })
            .join();
    }

    // Nothing it would have told runs once it is gone.
    turnTheRunLoop();
    check(listener.latencies == 0);
    check(listener.infos == 0);
};
} // namespace
} // namespace HostedAUTests
