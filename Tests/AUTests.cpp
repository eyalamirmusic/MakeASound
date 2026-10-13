// The AU adapter hosted in-process through AudioToolbox's C API: component
// types, the factory, channel negotiation, a whole render, parameters and their
// text, the edit gate and gesture events, state through ClassInfo, MIDI in and
// out, Reset, latency and the Cocoa view.

#include "AUTestHost.h"
#include "JuceFixtures.h"
#include "TamberPlugFixtures.h"

#include <MakeASound/Plugin/AU/CocoaUI.h>
#include <MakeASound/Plugin/AU/HostParameters.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <objc/runtime.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

using namespace nano;
using namespace TestPlugins;
using namespace AUHost;

namespace
{
using Module::ScopedStandIn;

struct LatencyParams : ParameterGroup
{
    LatencyParams() { add(latency); }

    FloatParam latency {"Latency", 0.f, 1000.f, 0.f, {.versionHint = 1}};
};

// latencySamples() is a parameter's value.
struct LatencyPlugin : StatePlugin<State<LatencyParams>>
{
    std::string_view name() const override { return "Latency"; }

    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    int latencySamples() const noexcept override
    {
        return static_cast<int>(std::lround(params.latency.getValue()));
    }

    int tailSamples() const noexcept override { return tail; }

    void announceLatency() { notifyHostLatencyChanged(); }

    int tail = 0;
};

// One parameter per curve an AU host can be told of, and the steps it can take.
struct CurveParams : ParameterGroup
{
    CurveParams()
    {
        add(squareRoot, squared, cubed, cubeRoot, symmetric, skewed);
        add(steppedFloat, wholeFloat, count);
    }

    static constexpr auto hint = ParameterOptions {.versionHint = 1};

    FloatParam squareRoot {"Square root", {10.f, 110.f, 0.f, 0.5f}, 20.f, hint};
    FloatParam squared {"Squared", {-5.f, 5.f, 0.f, 2.f}, 0.f, hint};
    FloatParam cubed {"Cubed", {0.f, 8.f, 0.f, 3.f}, 1.f, hint};
    FloatParam cubeRoot {"Cube root", {0.f, 1.f, 0.f, 1.f / 3.f}, 0.1f, hint};
    FloatParam symmetric {"Symmetric", {-1.f, 1.f, 0.f, 0.5f, true}, 0.f, hint};
    FloatParam skewed {"Skewed", {0.f, 100.f, 0.f, 0.3f}, 10.f, hint};
    FloatParam steppedFloat {"Stepped float", {0.f, 1.f, 0.25f}, 0.5f, hint};
    FloatParam wholeFloat {"Whole float", {2.f, 10.f, 1.f}, 4.f, hint};
    IntParam count {"Count", 1, 8, 2, hint};

    // The gain plugin's DSP reads this.
    DecibelParam gain {"Gain", -60.f, 12.f, 0.f, hint};
};

using CurvePlugin = GainPluginOf<CurveParams>;

// Sounds 1 while any note is held; reset() lets go of every one.
struct HoldingSynth : Plugin
{
    std::string_view name() const override { return "Holding"; }

    BusLayout getBusLayout() const override { return BusLayout::instrument(); }

    void prepare(const ProcessSpec&) override {}

    void reset() noexcept override
    {
        held = 0;
        ++resets;
    }

    void process(ProcessContext& context) noexcept override
    {
        for (const auto& event: context.mainMidiIn())
        {
            if (event.isNoteOn())
                ++held;
            else if (event.isNoteOff())
                held = std::max(0, held - 1);
        }

        context.mainOutput().fill(held > 0 ? 1.f : 0.f);
    }

    int held = 0;
    int resets = 0;
};

// Two stereo outputs: bus 0 reads 0.25 and bus 1 0.75.
struct TwoOutputSynth : Plugin
{
    std::string_view name() const override { return "TwoOutputs"; }

    BusLayout getBusLayout() const override
    {
        auto layout = BusLayout::instrument();
        layout.outputs.add({"Aux", 2, false});
        return layout;
    }

    void prepare(const ProcessSpec&) override {}

    void process(ProcessContext& context) noexcept override
    {
        ++processed;
        context.outputs[0].fill(0.25f);
        context.outputs[1].fill(0.75f);
    }

    int processed = 0;
};

// Records every controller value it is handed, in order.
struct CountingSynth : Plugin
{
    std::string_view name() const override { return "Counting"; }

    BusLayout getBusLayout() const override { return BusLayout::instrument(); }

    void prepare(const ProcessSpec&) override {}

    void process(ProcessContext& context) noexcept override
    {
        for (const auto& event: context.mainMidiIn())
            if (const auto* cc = event.asControlChange();
                cc != nullptr && numValues < values.size())
                values[numValues++] = static_cast<int>(std::lround(cc->value * 127));
    }

    std::array<int, 256> values {};
    int numValues = 0;
};

// A host transport at a fixed timeline position.
struct Transport
{
    static OSStatus state(void* userData,
                          Boolean* isPlaying,
                          Boolean* changed,
                          Float64* sampleInTimeline,
                          Boolean* isCycling,
                          Float64* cycleStart,
                          Float64* cycleEnd)
    {
        const auto& transport = *static_cast<const Transport*>(userData);
        *isPlaying = true;
        *changed = false;
        *sampleInTimeline = transport.sampleInTimeline;
        *isCycling = false;
        *cycleStart = 0.0;
        *cycleEnd = 0.0;
        return noErr;
    }

    Float64 sampleInTimeline = 0.0;
};

bool near(double a, double b, double tolerance = 1e-4)
{
    return std::abs(a - b) < tolerance;
}

template <typename Predicate>
bool pumpUntil(Predicate ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {5000});
}

std::string toUtf8(CFStringRef text)
{
    if (text == nullptr)
        return {};

    auto buffer = std::array<char, 256> {};
    CFStringGetCString(text, buffer.data(), buffer.size(), kCFStringEncodingUTF8);
    return buffer.data();
}

// Owns a +1 Core Foundation reference.
template <typename T>
struct CFOwned
{
    explicit CFOwned(T ref = nullptr)
        : value(ref)
    {
    }

    ~CFOwned()
    {
        if (value != nullptr)
            CFRelease(value);
    }

    CFOwned(const CFOwned&) = delete;
    CFOwned& operator=(const CFOwned&) = delete;

    T value;
};

template <typename P>
CFPropertyListRef copyClassInfo(UnitHost<P>& host)
{
    auto info = CFPropertyListRef {};
    auto size = UInt32 {sizeof(info)};
    check(AudioUnitGetProperty(host.unit,
                               kAudioUnitProperty_ClassInfo,
                               kAudioUnitScope_Global,
                               0,
                               &info,
                               &size)
          == noErr);
    return info;
}

template <typename P>
OSStatus restoreClassInfo(UnitHost<P>& host, CFPropertyListRef info)
{
    return AudioUnitSetProperty(host.unit,
                                kAudioUnitProperty_ClassInfo,
                                kAudioUnitScope_Global,
                                0,
                                &info,
                                sizeof(info));
}

void moveEveryParameter(GainParams& params)
{
    params.gain.setValue(-12.f);
    params.filter.cutoff.setValue(440.f);
    params.mode.setValue(2.f);
    params.bypass.setValue(1.f);
    params.width.setValue(0.9f);
}

void fillRamp(HostBus& bus)
{
    for (auto ch = 0; ch < bus.numChannels(); ++ch)
        for (auto s = 0; s < maxBlock; ++s)
            bus.storage[ch][s] = 0.01f * static_cast<float>(s + ch);
}

auto tComponentType = test("AU/componentTypesFollowTheCategory") = []
{
    auto module = describeModule();
    auto plugin = module.plugins[0];

    using Kind = MakeASound::Category;

    auto typeOf = [&](Kind category, PluginCreateFn create)
    {
        plugin.category = category;
        plugin.create = std::move(create);
        return AU::componentInfoFor(module, plugin).type;
    };

    auto effect = [] { return OwningPointer<Plugin>(EA::makeOwned<GainPlugin>()); };
    auto withMidiIn = []
    { return OwningPointer<Plugin>(EA::makeOwned<SynthPlugin>()); };

    check(typeOf(Kind::Effect, effect) == kAudioUnitType_Effect);
    check(typeOf(Kind::Effect, withMidiIn) == kAudioUnitType_MusicEffect);
    check(typeOf(Kind::Instrument, withMidiIn) == kAudioUnitType_MusicDevice);
    check(typeOf(Kind::MidiEffect, withMidiIn) == kAudioUnitType_MIDIProcessor);

    auto gain = AU::componentInfoFor(module, module.plugins[0]);
    check(gain.subtype == FourCC("Gain").toUint32());
    check(gain.manufacturer == FourCC("MaSo").toUint32());
    check(gain.numInputBuses == 1 && gain.numOutputBuses == 1);

    auto echo = AU::componentInfoFor(module, module.plugins[1]);
    check(echo.type == kAudioUnitType_MusicDevice);
    check(echo.numInputBuses == 0 && echo.numOutputBuses == 1);

    check(AU::fourCCString(kAudioUnitType_MusicDevice) == "aumu");
    check(AU::fourCCString(FourCC("Echo").toUint32()) == "Echo");
    check(AU::findPlugin(module, FourCC("Echo").toUint32()) == &module.plugins[1]);
    check(AU::findPlugin(module, FourCC("Nope").toUint32()) == nullptr);
    check(AU::versionNumber("1.2.3") == 0x010203);
    check(AU::versionNumber("2") == 0x020000);
};

auto tFactory = test("AU/theFactoryInstantiatesEveryPluginOfTheModule") = []
{
    for (auto code: {FourCC("Gain"), FourCC("Echo")})
    {
        auto expected = descriptionFor(code);
        auto unit = AudioComponentInstance {};
        check(AudioComponentInstanceNew(componentFor(code), &unit) == noErr);

        auto actual = AudioComponentDescription {};
        check(AudioComponentGetDescription(AudioComponentInstanceGetComponent(unit),
                                           &actual)
              == noErr);
        check(actual.componentType == expected.componentType);
        check(actual.componentSubType == code.toUint32());
        check(actual.componentManufacturer == FourCC("MaSo").toUint32());

        check(Module::lastCreated != nullptr);
        check(Module::lastCreated->format() == PluginFormat::AU);
        check(Module::lastCreated->name()
              == (code == FourCC("Gain") ? "Gain" : "Synth"));

        AudioComponentInstanceDispose(unit);
    }
};

auto tFactoryRefuses = test("AU/theFactoryRefusesCodesItDoesNotShip") = []
{
    auto unknown = descriptionFor("Gain");
    unknown.componentSubType = FourCC("Nope").toUint32();

    auto otherMaker = descriptionFor("Gain");
    otherMaker.componentManufacturer = FourCC("Othr").toUint32();

    auto wrongType = descriptionFor("Gain");
    wrongType.componentType = kAudioUnitType_Output;

    for (const auto& desc: {unknown, otherMaker, wrongType})
    {
        check(MakeASoundAUFactory(&desc) == nullptr);

        auto unit = AudioComponentInstance {};
        check(AudioComponentInstanceNew(registerComponent(desc), &unit) != noErr);
        check(unit == nullptr);
    }

    check(MakeASoundAUFactory(nullptr) == nullptr);
};

auto tSupportedChannels = test("AU/supportedChannelsAreWhatThePluginAccepts") = []
{
    auto supported = [](AudioComponentInstance unit)
    {
        auto size = UInt32 {};
        auto writable = Boolean {};
        check(AudioUnitGetPropertyInfo(unit,
                                       kAudioUnitProperty_SupportedNumChannels,
                                       kAudioUnitScope_Global,
                                       0,
                                       &size,
                                       &writable)
              == noErr);

        auto infos = std::vector<AUChannelInfo>(size / sizeof(AUChannelInfo));
        AudioUnitGetProperty(unit,
                             kAudioUnitProperty_SupportedNumChannels,
                             kAudioUnitScope_Global,
                             0,
                             infos.data(),
                             &size);

        auto pairs = std::vector<std::pair<int, int>> {};

        for (const auto& info: infos)
            pairs.emplace_back(info.inChannels, info.outChannels);

        return pairs;
    };

    auto gain = UnitHost<GainPlugin>("Gain", Start::Open);
    check(supported(gain.unit) == std::vector<std::pair<int, int>> {{1, 1}, {2, 2}});

    auto echo = UnitHost<SynthPlugin>("Echo", Start::Open);
    check(supported(echo.unit) == std::vector<std::pair<int, int>> {{0, 2}});

    for (auto scope: {kAudioUnitScope_Input, kAudioUnitScope_Output})
    {
        auto format = gain.format(scope, 0);
        check(format.mChannelsPerFrame == 2);
        check(format.mFormatID == kAudioFormatLinearPCM);
        check((format.mFormatFlags & kAudioFormatFlagIsFloat) != 0);
        check((format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0);
    }
};

auto tMono = test("AU/monoIsReachedOneElementAtATime") = []
{
    auto host = UnitHost<GainPlugin>("Gain", Start::Open);

    check(host.setChannels(kAudioUnitScope_Input, 0, 1) == noErr);
    check(host.setChannels(kAudioUnitScope_Output, 0, 1) == noErr);
    check(host.initialize() == noErr);

    const auto& layout = host.plugin().spec.layout;
    check(layout.getMainInputChannels() == 1);
    check(layout.getMainOutputChannels() == 1);

    fillRamp(host.inputs[0]);
    check(host.render(maxBlock) == noErr);
    check(host.outputs[0].storage[0][10] == host.inputs[0].storage[0][10]);

    check(host.setChannels(kAudioUnitScope_Input, 0, 2) != noErr);
};

auto tRefusedPair = test("AU/aRefusedPairFailsInitialize") = []
{
    auto host = UnitHost<GainPlugin>("Gain", Start::Open);

    check(host.setChannels(kAudioUnitScope_Input, 0, 3) != noErr);
    check(host.setChannels(kAudioUnitScope_Input, 0, 1) == noErr);
    check(host.initialize() != noErr);
    check(host.plugin().spec.sampleRate == 0);

    check(host.setChannels(kAudioUnitScope_Output, 0, 1) == noErr);
    check(host.initialize() == noErr);

    auto echo = UnitHost<SynthPlugin>("Echo", Start::Open);
    check(echo.setChannels(kAudioUnitScope_Output, 0, 1) != noErr);
};

auto tPrepare = test("AU/initializePreparesThePlugin") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& plugin = host.plugin();

    check(plugin.spec.sampleRate == 48000);
    check(plugin.spec.maxBlockSize == maxBlock);
    check(plugin.spec.layout == BusLayout::stereoInOut());
    check(plugin.formatAtConstruction == PluginFormat::Unknown);
    check(plugin.formatAtPrepare == PluginFormat::AU);
    check(plugin.resets == 1);

    check(host.initialize(44100.0, 32) == noErr);
    check(plugin.spec.sampleRate == 44100);
    check(plugin.spec.maxBlockSize == 32);
};

auto tUnity = test("AU/aBlockAtUnityPassesTheInput") = []
{
    for (auto inPlace: {false, true})
    {
        auto host = UnitHost<GainPlugin>("Gain");
        host.inPlace = inPlace;
        fillRamp(host.inputs[0]);
        host.outputs[0].fill(-1.f);

        auto expected = host.inputs[0].storage;

        check(host.render(maxBlock) == noErr);
        check(host.pulls == 1);
        check(host.plugin().processed == 1);

        for (auto ch = 0; ch < 2; ++ch)
            for (auto s = 0; s < maxBlock; ++s)
                check(host.outputs[0].storage[ch][s] == expected[ch][s]);

        check(host.plugin().seenInput[1][5] == expected[1][5]);
        check(host.plugin().boundSamples == 4 * maxBlock);
    }
};

auto tUnitBuffers = test("AU/aBlockIntoTheUnitsOwnBuffers") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    fillRamp(host.inputs[0]);

    check(host.render(maxBlock, false) == noErr);
    check(host.rendered->mBuffers[0].mData != nullptr);
    check(host.rendered->mBuffers[0].mData != host.outputs[0].storage[0].data());

    for (auto s = 0; s < maxBlock; ++s)
        check(host.renderedSample(1, s) == host.inputs[0].storage[1][s]);
};

auto tGainApplies = test("AU/aHostWriteLandsInTheNextBlock") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    fillRamp(host.inputs[0]);

    check(host.hostWrites(host.hostIdOf(gain), -6.f) == noErr);
    check(near(host.hostValue(host.hostIdOf(gain)), -6.f));

    check(host.render(maxBlock) == noErr);
    check(near(gain.getValue(), -6.f));

    auto factor = gain.gain();
    check(near(host.outputs[0].storage[0][20],
               host.inputs[0].storage[0][20] * factor));
};

auto tUnconnected = test("AU/anUnconnectedInputRendersSilence") = []
{
    auto host = UnitHost<GainPlugin>("Gain", Start::Open);
    host.connectInputs = false;
    check(host.initialize() == noErr);

    host.outputs[0].fill(1.f);
    check(host.render(maxBlock) == noErr);
    check(host.plugin().processed == 1);

    for (const auto& channel: host.outputs[0].storage)
        for (auto s = 0; s < maxBlock; ++s)
            check(channel[s] == 0.f);
};

auto tRenderLimits = test("AU/aRenderOutsideTheContractIsRefused") = []
{
    auto host = UnitHost<GainPlugin>("Gain", Start::Open);
    host.outputs.emplace_back(2);

    check(host.render(maxBlock) == kAudioUnitErr_Uninitialized);
    check(host.plugin().processed == 0);

    check(host.initialize() == noErr);
    check(host.render(maxBlock + 1) == kAudioUnitErr_TooManyFramesToProcess);
    check(host.plugin().processed == 0);

    check(host.render(0) == noErr);
};

auto tPlayhead = test("AU/thePlayheadCarriesTheTimelinePosition") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto transport = Transport {.sampleInTimeline = 96000.0};

    auto callbacks = HostCallbackInfo {};
    callbacks.hostUserData = &transport;
    callbacks.transportStateProc = &Transport::state;
    check(AudioUnitSetProperty(host.unit,
                               kAudioUnitProperty_HostCallbacks,
                               kAudioUnitScope_Global,
                               0,
                               &callbacks,
                               sizeof(callbacks))
          == noErr);

    host.sampleTime = 4800.0;
    check(host.render(maxBlock) == noErr);

    const auto& playhead = host.plugin().playhead;
    check(playhead.isValid && playhead.isPlaying);
    check(playhead.sampleTime == 96000);
};

auto tPlayheadFallback = test("AU/withNoTransportThePlayheadIsTheStreamClock") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    host.sampleTime = 4800.0;

    check(host.render(maxBlock) == noErr);
    check(host.plugin().playhead.sampleTime == 4800);
};

auto tTwoOutputs = test("AU/everyOutputBusCarriesItsBlockWhicheverRendersFirst") = []
{
    auto standIn = ScopedStandIn<TwoOutputSynth>(Module::echoStandIn);
    auto host = UnitHost<TwoOutputSynth>("Echo");
    check(host.outputs.size() == 2);

    for (auto& bus: host.outputs)
        bus.fill(-1.f);

    check(host.render(maxBlock, true, 1) == noErr);
    host.sampleTime -= maxBlock;
    check(host.render(maxBlock, true, 0) == noErr);
    check(host.plugin().processed == 1);

    for (auto bus = 0; bus < 2; ++bus)
        for (const auto& channel: host.outputs[bus].storage)
            for (auto s = 0; s < maxBlock; ++s)
                check(channel[s] == (bus == 0 ? 0.25f : 0.75f));
};

std::vector<AudioUnitParameterID> parameterListOf(AudioUnit unit)
{
    auto size = UInt32 {};
    auto writable = Boolean {};
    AudioUnitGetPropertyInfo(unit,
                             kAudioUnitProperty_ParameterList,
                             kAudioUnitScope_Global,
                             0,
                             &size,
                             &writable);

    auto ids =
        std::vector<AudioUnitParameterID>(size / sizeof(AudioUnitParameterID));
    AudioUnitGetProperty(unit,
                         kAudioUnitProperty_ParameterList,
                         kAudioUnitScope_Global,
                         0,
                         ids.data(),
                         &size);
    return ids;
}

// JUCE's order: by (versionHint, host id).
std::vector<AudioUnitParameterID> expectedListOrder(const ParameterList& list)
{
    auto keyed = std::vector<std::pair<int, AudioUnitParameterID>> {};

    for (auto i = 0; i < list.size(); ++i)
        if (list.isHostExposed(i))
            keyed.emplace_back(list[i].versionHint(), list.entry(i).hostId);

    std::sort(keyed.begin(), keyed.end());

    auto ids = std::vector<AudioUnitParameterID> {};

    for (const auto& [hint, id]: keyed)
        ids.push_back(id);

    return ids;
}

// The value an AU host reads and writes: plain, or 0..1 for a curve no display
// flag draws.
float hostSpace(const Parameter& param, float plain)
{
    return param.curve() == Parameter::Curve::Other ? param.toNormalized(plain)
                                                    : plain;
}

auto tParameterList =
    test("AU/theParameterListIsOrderedByVersionHintThenHostId") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    const auto& list = host.plugin().parameters();

    auto expected = expectedListOrder(list);
    check(parameterListOf(host.unit) == expected);

    auto size = UInt32 {};
    auto writable = Boolean {};
    check(AudioUnitGetPropertyInfo(host.unit,
                                   kAudioUnitProperty_ParameterList,
                                   kAudioUnitScope_Global,
                                   0,
                                   &size,
                                   &writable)
          == noErr);

    auto ids =
        std::vector<AudioUnitParameterID>(size / sizeof(AudioUnitParameterID));
    check(AudioUnitGetProperty(host.unit,
                               kAudioUnitProperty_ParameterList,
                               kAudioUnitScope_Global,
                               0,
                               ids.data(),
                               &size)
          == noErr);

    check(static_cast<int>(ids.size()) == list.size());
    check(ids == expected);

    auto free = AudioUnitParameterID {12345};
    check(list.indexOfHostId(free) < 0);
    check(host.hostWrites(free, 0.5f) == kAudioUnitErr_InvalidParameter);
};

auto tParameterAdded =
    test("AU/aParameterAddedWithAHigherHintKeepsEveryOldIndex") = []
{
    auto older = std::vector<AudioUnitParameterID> {};

    {
        auto host = UnitHost<GainPlugin>("Gain");
        older = parameterListOf(host.unit);
    }

    auto newer = ScopedStandIn<NewerGainPlugin>(Module::gainStandIn);
    auto host = UnitHost<NewerGainPlugin>("Gain");
    auto ids = parameterListOf(host.unit);

    // Drive is declared second but carries hint 2, so it is listed last.
    check(ids.size() == older.size() + 1);
    check(std::equal(older.begin(), older.end(), ids.begin()));
    check(ids.back() == host.hostIdOf(host.plugin().params.drive));
};

auto tParameterInfo = test("AU/parameterInfoMirrorsTheList") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    const auto& list = host.plugin().parameters();
    const auto& params = host.plugin().params;

    for (auto i = 0; i < list.size(); ++i)
    {
        const auto& entry = list.entry(i);
        const auto& param = list[i];

        auto info = AudioUnitParameterInfo {};
        auto size = UInt32 {sizeof(info)};
        check(AudioUnitGetProperty(host.unit,
                                   kAudioUnitProperty_ParameterInfo,
                                   kAudioUnitScope_Global,
                                   entry.hostId,
                                   &info,
                                   &size)
              == noErr);

        check((info.flags & kAudioUnitParameterFlag_HasCFNameString) != 0);
        check(toUtf8(info.cfNameString) == entry.displayName);

        if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) != 0)
            CFRelease(info.cfNameString);

        // The cutoff's skew is no display flag's curve: 0..1.
        auto normalized = &param == &params.filter.cutoff;
        check(normalized == (param.curve() == Parameter::Curve::Other));
        check(info.minValue == (normalized ? 0.f : param.minValue()));
        check(info.maxValue == (normalized ? 1.f : param.maxValue()));
        check(info.defaultValue == hostSpace(param, param.defaultValue()));
        check((info.flags & kAudioUnitParameterFlag_IsReadable) != 0);
        check((info.flags & kAudioUnitParameterFlag_IsWritable) != 0);
        check((info.flags & kAudioUnitParameterFlag_IsHighResolution) != 0);
        check((info.flags & kAudioUnitParameterFlag_DisplayMask) == 0);

        auto hasStrings = &param == &params.mode || &param == &params.bypass;
        check(((info.flags & kAudioUnitParameterFlag_ValuesHaveStrings) != 0)
              == hasStrings);

        auto expectedUnit = &param == &params.mode ? kAudioUnitParameterUnit_Indexed
                            : &param == &params.bypass
                                ? kAudioUnitParameterUnit_Boolean
                                : kAudioUnitParameterUnit_Generic;
        check(info.unit == expectedUnit);

        check(near(host.hostValue(entry.hostId),
                   hostSpace(param, param.defaultValue())));
    }

    auto info = AudioUnitParameterInfo {};
    auto size = UInt32 {sizeof(info)};
    check(AudioUnitGetProperty(host.unit,
                               kAudioUnitProperty_ParameterInfo,
                               kAudioUnitScope_Global,
                               12345,
                               &info,
                               &size)
          != noErr);
};

struct HintParams : ParameterGroup
{
    HintParams() { add(b, a, hidden, c, unset); }

    FloatParam b {"B", 0.f, 1.f, 0.f, {.hostId = 20, .versionHint = 1}};
    FloatParam a {"A", 0.f, 1.f, 0.f, {.hostId = 30, .versionHint = 1}};
    FloatParam hidden {"Hidden", 0.f, 1.f, 0.f, {.automatable = false, .hostId = 5}};
    FloatParam c {"C", 0.f, 1.f, 0.f, {.hostId = 10, .versionHint = 2}};
    FloatParam unset {"Unset", 0.f, 1.f, 0.f, {.hostId = 40}};
};

// The two pieces the adapter's list and its debug assert are made of.
auto tListOrder = test("AU/theListOrderAndTheUnhintedLookup") = []
{
    auto params = HintParams {};
    auto list = ParameterList {params};
    auto at = [&](const Parameter& param) { return list.indexOf(&param); };

    // (hint, host id): the unset one first, then hint 1 by id, then hint 2;
    // the hidden one is not the host's.
    auto order = AU::parameterListOrder(list);
    check(order.size() == 4);
    check(order[0] == at(params.unset));
    check(order[1] == at(params.b));
    check(order[2] == at(params.a));
    check(order[3] == at(params.c));

    check(AU::firstUnhintedParameter(list, -1) == at(params.unset));
    check(AU::firstUnhintedParameter(list, at(params.unset)) == -1);
};

auto tCurves = test("AU/aCurveIsADisplayFlagOrTheRangeIsZeroToOne") = []
{
    auto curves = ScopedStandIn<CurvePlugin>(Module::gainStandIn);
    auto host = UnitHost<CurvePlugin>("Gain");
    auto& params = host.plugin().params;

    auto infoOf = [&](const Parameter& param)
    {
        auto info = AudioUnitParameterInfo {};
        auto size = UInt32 {sizeof(info)};
        check(AudioUnitGetProperty(host.unit,
                                   kAudioUnitProperty_ParameterInfo,
                                   kAudioUnitScope_Global,
                                   host.hostIdOf(param),
                                   &info,
                                   &size)
              == noErr);

        if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) != 0)
            CFRelease(info.cfNameString);

        return info;
    };

    struct Flagged
    {
        FloatParam* param;
        AudioUnitParameterOptions flag;
    };

    // The plain range, and the host's own maths for the flag lands where the
    // parameter's does.
    for (auto [param, flag]:
         {Flagged {&params.squareRoot, kAudioUnitParameterFlag_DisplaySquareRoot},
          Flagged {&params.squared, kAudioUnitParameterFlag_DisplaySquared},
          Flagged {&params.cubed, kAudioUnitParameterFlag_DisplayCubed},
          Flagged {&params.cubeRoot, kAudioUnitParameterFlag_DisplayCubeRoot}})
    {
        auto info = infoOf(*param);
        check((info.flags & kAudioUnitParameterFlag_DisplayMask) == flag);
        check(info.minValue == param->minValue());
        check(info.maxValue == param->maxValue());
        check(info.unit == kAudioUnitParameterUnit_Generic);

        auto auParam = AudioUnitParameter {
            host.unit, host.hostIdOf(*param), kAudioUnitScope_Global, 0};

        for (auto position: {0.1f, 0.25f, 0.5f, 0.8f})
        {
            auto plain = param->toPlain(position);
            check(near(AUParameterValueToLinear(plain, &auParam),
                       param->toNormalized(plain),
                       1e-4));
        }
    }

    // No flag draws these: 0..1, and every value crosses in that space.
    for (auto* param: {&params.symmetric, &params.skewed})
    {
        auto info = infoOf(*param);
        auto id = host.hostIdOf(*param);
        check((info.flags & kAudioUnitParameterFlag_DisplayMask) == 0);
        check(info.minValue == 0.f && info.maxValue == 1.f);
        check(info.defaultValue == param->toNormalized(param->defaultValue()));

        check(host.hostWrites(id, 0.3f) == noErr);
        check(param->getValue() == param->toPlain(0.3f));
        check(host.render(maxBlock) == noErr);
        check(host.hostValue(id) == 0.3f);

        param->setValue(param->toPlain(0.7f));
        check(host.render(maxBlock) == noErr);
        check(near(host.hostValue(id), param->toNormalized(param->getValue())));

        auto plain = param->toPlain(0.7f);
        auto toText = AudioUnitParameterStringFromValue {};
        auto value = 0.7f;
        toText.inParamID = id;
        toText.inValue = &value;
        auto size = UInt32 {sizeof(toText)};
        check(AudioUnitGetProperty(host.unit,
                                   kAudioUnitProperty_ParameterStringFromValue,
                                   kAudioUnitScope_Global,
                                   0,
                                   &toText,
                                   &size)
              == noErr);
        auto text = CFOwned<CFStringRef>(toText.outString);
        check(toUtf8(text.value) == param->valueToText(plain));
    }

    // Indexed only where each step is a whole number apart.
    check(infoOf(params.steppedFloat).unit == kAudioUnitParameterUnit_Generic);
    check(infoOf(params.wholeFloat).unit == kAudioUnitParameterUnit_Indexed);
    check(infoOf(params.count).unit == kAudioUnitParameterUnit_Indexed);
    check(infoOf(params.count).maxValue == 8.f);

    for (auto* param: std::initializer_list<const Parameter*> {
             &params.steppedFloat, &params.wholeFloat, &params.count})
        check((infoOf(*param).flags & kAudioUnitParameterFlag_IsHighResolution)
              != 0);
};

auto tValueStrings = test("AU/valueStringsNameAChoicesSteps") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& params = host.plugin().params;

    auto copyStrings = [&](const Parameter& param, OSStatus& result)
    {
        auto strings = CFArrayRef {};
        auto size = UInt32 {sizeof(strings)};
        result = AudioUnitGetProperty(host.unit,
                                      kAudioUnitProperty_ParameterValueStrings,
                                      kAudioUnitScope_Global,
                                      host.hostIdOf(param),
                                      &strings,
                                      &size);
        return strings;
    };

    auto result = OSStatus {};
    auto modes = CFOwned<CFArrayRef>(copyStrings(params.mode, result));
    check(result == noErr && modes.value != nullptr);
    check(CFArrayGetCount(modes.value) == 3);

    auto names = std::array<std::string, 3> {"Clean", "Warm", "Hot"};

    for (auto i = 0; i < 3; ++i)
        check(
            toUtf8(static_cast<CFStringRef>(CFArrayGetValueAtIndex(modes.value, i)))
            == names[i]);

    auto bypass = CFOwned<CFArrayRef>(copyStrings(params.bypass, result));
    check(result == noErr && CFArrayGetCount(bypass.value) == 2);

    copyStrings(params.gain, result);
    check(result != noErr);
};

auto tTextConversions = test("AU/textAndValuesGoThroughTheParameter") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& params = host.plugin().params;

    for (auto* param: std::initializer_list<Parameter*> {
             &params.gain, &params.filter.cutoff, &params.mode})
    {
        auto plain = param->toPlain(0.4f);
        auto value = hostSpace(*param, plain);

        auto toText = AudioUnitParameterStringFromValue {};
        toText.inParamID = host.hostIdOf(*param);
        toText.inValue = &value;
        auto size = UInt32 {sizeof(toText)};
        check(AudioUnitGetProperty(host.unit,
                                   kAudioUnitProperty_ParameterStringFromValue,
                                   kAudioUnitScope_Global,
                                   0,
                                   &toText,
                                   &size)
              == noErr);

        auto text = CFOwned<CFStringRef>(toText.outString);
        auto expected = param->valueToText(plain);
        check(toUtf8(text.value) == expected);

        auto fromText = AudioUnitParameterValueFromString {};
        fromText.inParamID = toText.inParamID;
        fromText.inString = text.value;
        size = sizeof(fromText);
        check(AudioUnitGetProperty(host.unit,
                                   kAudioUnitProperty_ParameterValueFromString,
                                   kAudioUnitScope_Global,
                                   0,
                                   &fromText,
                                   &size)
              == noErr);
        check(near(fromText.outValue,
                   hostSpace(*param, param->textToValue(expected)),
                   1e-2));
    }

    params.gain.setValue(-3.f);

    auto current = AudioUnitParameterStringFromValue {};
    current.inParamID = host.hostIdOf(params.gain);
    auto size = UInt32 {sizeof(current)};
    check(AudioUnitGetProperty(host.unit,
                               kAudioUnitProperty_ParameterStringFromValue,
                               kAudioUnitScope_Global,
                               0,
                               &current,
                               &size)
          == noErr);

    auto text = CFOwned<CFStringRef>(current.outString);
    check(toUtf8(text.value) == params.gain.valueToText(-3.f));
};

auto tTextNotANumber = test("AU/textThatIsNoNumberKeepsTheCurrentValue") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& width = host.plugin().params.width;
    width.setValue(0.7f);

    for (auto* typed: {CFSTR("nan"), CFSTR("inf")})
    {
        auto fromText = AudioUnitParameterValueFromString {};
        fromText.inParamID = host.hostIdOf(width);
        fromText.inString = typed;
        auto size = UInt32 {sizeof(fromText)};
        check(AudioUnitGetProperty(host.unit,
                                   kAudioUnitProperty_ParameterValueFromString,
                                   kAudioUnitScope_Global,
                                   0,
                                   &fromText,
                                   &size)
              == noErr);
        check(near(fromText.outValue, 0.7f));
    }
};

auto tOffGrid = test("AU/anOffGridHostWriteIsRetainedAsWritten") = []
{
    auto host = UnitHost<GainPlugin>("Gain", Start::Open);
    auto& mode = host.plugin().params.mode;
    auto modeId = host.hostIdOf(mode);

    check(host.hostWrites(modeId, 1.4f) == noErr);
    check(host.initialize() == noErr);

    check(host.hostValue(modeId) == 1.4f);
    check(mode.getIndex() == 1);

    check(host.render(maxBlock) == noErr);
    check(host.hostValue(modeId) == 1.4f);
    check(mode.getIndex() == 1);
};

auto tPluginMoves = test("AU/aValueThePluginMovesIsMirroredToTheHost") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    auto gainId = host.hostIdOf(gain);

    gain.setValue(-9.f);
    check(host.render(maxBlock) == noErr);
    check(near(host.hostValue(gainId), -9.f));

    check(host.hostWrites(gainId, -4.f) == noErr);
    check(host.render(maxBlock) == noErr);
    check(near(gain.getValue(), -4.f));
};

auto tEditGate = test("AU/aHeldParameterDropsTheHostWrite") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    auto gainId = host.hostIdOf(gain);
    auto& listener = *host.plugin().hostEditListener();
    check(host.render(maxBlock) == noErr);

    listener.beginParameterEdit(0);
    gain.setValue(-20.f);
    listener.performParameterEdit(0, gain.getNormalized());
    check(near(host.hostValue(gainId), -20.f));

    check(host.hostWrites(gainId, -3.f) == noErr);
    check(host.render(maxBlock) == noErr);
    check(near(gain.getValue(), -20.f));
    check(near(host.hostValue(gainId), -20.f));

    listener.endParameterEdit(0);

    check(host.hostWrites(gainId, -5.f) == noErr);
    check(host.render(maxBlock) == noErr);
    check(near(gain.getValue(), -5.f));
};

struct GestureLog
{
    std::vector<AudioUnitEventType> types;
    std::vector<AudioUnitParameterID> ids;
    std::vector<float> values;
};

void recordGesture(void* userData,
                   void*,
                   const AudioUnitEvent* event,
                   UInt64,
                   AudioUnitParameterValue value)
{
    auto& log = *static_cast<GestureLog*>(userData);
    log.types.push_back(event->mEventType);
    log.ids.push_back(event->mArgument.mParameter.mParameterID);
    log.values.push_back(value);
}

auto tGestures = test("AU/gesturesReachAnEventListener") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    auto gainId = host.hostIdOf(gain);
    auto log = GestureLog {};

    auto listener = AUEventListenerRef {};
    check(AUEventListenerCreate(&recordGesture,
                                &log,
                                CFRunLoopGetCurrent(),
                                kCFRunLoopDefaultMode,
                                0.0,
                                0.0,
                                &listener)
          == noErr);

    for (auto type: {kAudioUnitEvent_BeginParameterChangeGesture,
                     kAudioUnitEvent_ParameterValueChange,
                     kAudioUnitEvent_EndParameterChangeGesture})
    {
        auto event = AudioUnitEvent {};
        event.mEventType = type;
        event.mArgument.mParameter = {host.unit, gainId, kAudioUnitScope_Global, 0};
        check(AUEventListenerAddEventType(listener, &log, &event) == noErr);
    }

    auto& edits = *host.plugin().hostEditListener();
    edits.beginParameterEdit(0);
    gain.setValue(-15.f);
    edits.performParameterEdit(0, gain.getNormalized());
    edits.endParameterEdit(0);

    check(pumpUntil([&] { return log.types.size() >= 3; }));

    check(log.types.size() == 3);
    check(log.types[0] == kAudioUnitEvent_BeginParameterChangeGesture);
    check(log.types[1] == kAudioUnitEvent_ParameterValueChange);
    check(log.types[2] == kAudioUnitEvent_EndParameterChangeGesture);
    check(std::all_of(
        log.ids.begin(), log.ids.end(), [&](auto id) { return id == gainId; }));
    check(near(log.values[1], -15.f));

    AUListenerDispose(listener);
};

auto tStateRoundTrip = test("AU/stateRoundTripsThroughClassInfo") = []
{
    auto source = UnitHost<GainPlugin>("Gain");
    moveEveryParameter(source.plugin().params);
    source.plugin().state.preset = "Saved";
    source.plugin().state.markChanged();
    pumpUntil([&] { return source.plugin().isStateSnapshotCurrent(); });

    auto info = CFOwned<CFPropertyListRef>(copyClassInfo(source));
    check(CFGetTypeID(info.value) == CFDictionaryGetTypeID());

    auto* dictionary = static_cast<CFDictionaryRef>(info.value);
    auto* document = CFDictionaryGetValue(dictionary, CFSTR("MakeASoundState"));
    check(document != nullptr && CFGetTypeID(document) == CFDataGetTypeID());

    auto target = UnitHost<GainPlugin>("Gain");
    check(restoreClassInfo(target, info.value) == noErr);

    const auto& params = target.plugin().params;
    check(near(params.gain.getValue(), -12.f));
    check(near(params.filter.cutoff.getValue(), 440.f, 1e-2));
    check(params.mode.getIndex() == 2);
    check(params.bypass.isOn());
    check(near(params.width.getValue(), 0.9f));
    check(pumpUntil([&] { return target.plugin().state.preset == "Saved"; }));

    check(near(target.hostValue(target.hostIdOf(params.gain)), -12.f));
    check(near(target.hostValue(target.hostIdOf(params.mode)), 2.f));
};

auto tStateFromDocument = test("AU/classInfoFromDocumentRestoresLikeClassInfo") = []
{
    auto source = UnitHost<GainPlugin>("Gain");
    moveEveryParameter(source.plugin().params);
    auto info = CFOwned<CFPropertyListRef>(copyClassInfo(source));

    auto target = UnitHost<GainPlugin>("Gain");

    auto size = UInt32 {};
    auto writable = Boolean {};
    check(AudioUnitGetPropertyInfo(target.unit,
                                   kAudioUnitProperty_ClassInfoFromDocument,
                                   kAudioUnitScope_Global,
                                   0,
                                   &size,
                                   &writable)
          == noErr);
    check(size == sizeof(CFPropertyListRef) && writable);

    check(AudioUnitSetProperty(target.unit,
                               kAudioUnitProperty_ClassInfoFromDocument,
                               kAudioUnitScope_Global,
                               0,
                               &info.value,
                               sizeof(info.value))
          == noErr);

    const auto& params = target.plugin().params;
    check(near(params.gain.getValue(), -12.f));
    check(params.mode.getIndex() == 2);
    check(near(params.width.getValue(), 0.9f));
    check(near(target.hostValue(target.hostIdOf(params.gain)), -12.f));
};

auto tStateHostWrite = test("AU/aHostWriteBeforeAnyRenderIsSaved") = []
{
    auto source = UnitHost<GainPlugin>("Gain");
    check(source.hostWrites(source.hostIdOf(source.plugin().params.gain), -7.f)
          == noErr);

    auto info = CFOwned<CFPropertyListRef>(copyClassInfo(source));

    auto target = UnitHost<GainPlugin>("Gain");
    check(restoreClassInfo(target, info.value) == noErr);
    check(near(target.plugin().params.gain.getValue(), -7.f));
};

auto tStateInserted = test("AU/aParameterInsertedMidListKeepsTheOthers") = []
{
    auto source = UnitHost<GainPlugin>("Gain");
    moveEveryParameter(source.plugin().params);
    auto info = CFOwned<CFPropertyListRef>(copyClassInfo(source));

    auto newer = ScopedStandIn<NewerGainPlugin>(Module::gainStandIn);
    auto target = UnitHost<NewerGainPlugin>("Gain");
    check(restoreClassInfo(target, info.value) == noErr);

    const auto& params = target.plugin().params;
    check(near(params.gain.getValue(), -12.f));
    check(near(params.drive.getValue(), 0.25f));
    check(near(params.filter.cutoff.getValue(), 440.f, 1e-2));
    check(params.mode.getIndex() == 2);
    check(params.bypass.isOn());

    check(near(target.hostValue(target.hostIdOf(params.drive)), 0.25f));
    check(near(target.hostValue(target.hostIdOf(params.gain)), -12.f));
};

auto tStateOtherSubtype = test("AU/classInfoFromAnotherSubtypeIsRefused") = []
{
    auto gain = UnitHost<GainPlugin>("Gain");
    auto info = CFOwned<CFPropertyListRef>(copyClassInfo(gain));

    auto echo = UnitHost<SynthPlugin>("Echo");
    echo.plugin().params.level.setValue(0.8f);
    check(restoreClassInfo(echo, info.value) != noErr);
    check(near(echo.plugin().params.level.getValue(), 0.8f));
};

auto tSaveOffThread = test("AU/aSaveFromAWorkerReturnsWhileTheMainThreadSpins") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    host.plugin().params.gain.setValue(-6.f);

    auto info = CFPropertyListRef {};
    auto result = OSStatus {-1};
    auto done = std::atomic<bool> {false};

    auto saver = std::thread(
        [&]
        {
            auto size = UInt32 {sizeof(info)};
            result = AudioUnitGetProperty(host.unit,
                                          kAudioUnitProperty_ClassInfo,
                                          kAudioUnitScope_Global,
                                          0,
                                          &info,
                                          &size);
            done = true;
        });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (!done && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    check(done.load());
    saver.join();
    check(result == noErr && info != nullptr);

    auto owned = CFOwned<CFPropertyListRef>(info);
    auto target = UnitHost<GainPlugin>("Gain");
    check(restoreClassInfo(target, owned.value) == noErr);
    check(near(target.plugin().params.gain.getValue(), -6.f));
};

auto tNotes = test("AU/notesReachTheInstrumentAtTheirOffsets") = []
{
    auto host = UnitHost<SynthPlugin>("Echo");

    check(MusicDeviceMIDIEvent(host.unit, 0x82, 64, 64, 10) == noErr);
    check(MusicDeviceMIDIEvent(host.unit, 0x92, 64, 96, 5) == noErr);
    check(host.render(maxBlock) == noErr);

    const auto& plugin = host.plugin();
    check(plugin.numEvents == 2);
    check(plugin.events[0].isNoteOn() && plugin.events[0].sampleOffset == 5);
    check(plugin.events[0].channel == 2);
    check(plugin.events[0].asNoteOn()->pitch == 64);
    check(near(plugin.events[0].asNoteOn()->velocity, 96.0 / 127.0));
    check(plugin.events[1].isNoteOff() && plugin.events[1].sampleOffset == 10);

    for (const auto& channel: host.outputs[0].storage)
        check(channel[maxBlock - 1] == 0.5f);

    check(MusicDeviceMIDIEvent(host.unit, 0x90, 60, 0, 3) == noErr);
    check(host.render(maxBlock) == noErr);
    check(plugin.numEvents == 1 && plugin.events[0].isNoteOff());
    check(plugin.events[0].sampleOffset == 3);
};

auto tMidiFromAnotherThread = test("AU/midiFromAnotherThreadIsNeverLost") = []
{
    auto counting = ScopedStandIn<CountingSynth>(Module::echoStandIn);
    auto host = UnitHost<CountingSynth>("Echo");
    constexpr auto numEvents = 200;
    auto done = std::atomic<bool> {false};

    auto sender = std::thread(
        [&]
        {
            for (auto i = 0; i < numEvents; ++i)
            {
                MusicDeviceMIDIEvent(host.unit, 0xB0, 1, i % 128, 0);

                if (i % 8 == 0)
                    std::this_thread::yield();
            }

            done = true;
        });

    while (!done)
        check(host.render(maxBlock) == noErr);

    sender.join();
    check(host.render(maxBlock) == noErr);

    const auto& plugin = host.plugin();
    check(plugin.numValues == numEvents);

    for (auto i = 0; i < numEvents; ++i)
        check(plugin.values[i] == i % 128);
};

auto tChannelMode = test("AU/allNotesOffKeepsItsFrame") = []
{
    auto host = UnitHost<SynthPlugin>("Echo");

    check(MusicDeviceMIDIEvent(host.unit, 0xB1, 123, 0, 7) == noErr);
    check(MusicDeviceMIDIEvent(host.unit, 0xB1, 120, 0, 9) == noErr);
    check(MusicDeviceMIDIEvent(host.unit, 0xB1, 74, 127, 11) == noErr);
    check(host.render(maxBlock) == noErr);

    const auto& plugin = host.plugin();
    check(plugin.numEvents == 3);

    auto controllers = std::array<int, 3> {123, 120, 74};
    auto offsets = std::array<int, 3> {7, 9, 11};

    for (auto i = 0; i < 3; ++i)
    {
        const auto* cc = plugin.events[i].asControlChange();
        check(cc != nullptr && cc->controller == controllers[i]);
        check(plugin.events[i].sampleOffset == offsets[i]);
        check(plugin.events[i].channel == 1);
    }

    check(near(plugin.events[2].asControlChange()->value, 1.0));
};

auto tOtherMessages = test("AU/bendPressureAndProgramReachTheInstrument") = []
{
    auto host = UnitHost<SynthPlugin>("Echo");

    check(MusicDeviceMIDIEvent(host.unit, 0xE0, 0, 96, 1) == noErr);
    check(MusicDeviceMIDIEvent(host.unit, 0xD0, 64, 0, 2) == noErr);
    check(MusicDeviceMIDIEvent(host.unit, 0xA0, 60, 32, 3) == noErr);
    check(MusicDeviceMIDIEvent(host.unit, 0xC5, 10, 0, 4) == noErr);
    check(host.render(maxBlock) == noErr);

    const auto& plugin = host.plugin();
    check(plugin.numEvents == 4);
    check(plugin.events[0].isPitchBend());
    check(near(plugin.events[0].asPitchBend()->value, 0.5));
    check(plugin.events[1].isChannelAftertouch());
    check(near(plugin.events[1].asChannelAftertouch()->pressure, 64.0 / 127.0));
    check(plugin.events[2].sampleOffset == 3);

    const auto* program = plugin.events[3].asProgramChange();
    check(program != nullptr && program->program == 10);
    check(plugin.events[3].channel == 5);
};

auto tSysEx = test("AU/sysExArrivesWhole") = []
{
    auto host = UnitHost<SynthPlugin>("Echo");
    auto bytes = std::array<UInt8, 6> {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};

    check(MusicDeviceSysEx(host.unit, bytes.data(), bytes.size()) == noErr);
    check(host.render(maxBlock) == noErr);

    check(host.plugin().numEvents == 1);
    const auto* sysEx = host.plugin().events[0].asSysEx();
    check(sysEx != nullptr && sysEx->size == 6);
    check(std::equal(bytes.begin(), bytes.end(), sysEx->data.begin()));

    check(host.sent.size() == 1);
    check(host.sent[0].is({0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7}));
};

auto tMidiOut = test("AU/echoedEventsLeaveThroughTheCallbackAtTheirOffsets") = []
{
    auto host = UnitHost<SynthPlugin>("Echo");

    auto names = CFArrayRef {};
    auto size = UInt32 {sizeof(names)};
    check(AudioUnitGetProperty(host.unit,
                               kAudioUnitProperty_MIDIOutputCallbackInfo,
                               kAudioUnitScope_Global,
                               0,
                               &names,
                               &size)
          == noErr);

    auto owned = CFOwned<CFArrayRef>(names);
    check(CFArrayGetCount(names) == 1);
    check(toUtf8(static_cast<CFStringRef>(CFArrayGetValueAtIndex(names, 0)))
          == "MIDI Out");

    MusicDeviceMIDIEvent(host.unit, 0x92, 64, 96, 5);
    MusicDeviceMIDIEvent(host.unit, 0x82, 64, 64, 10);
    MusicDeviceMIDIEvent(host.unit, 0xB3, 123, 0, 12);
    check(host.render(maxBlock) == noErr);

    check(host.midiCallbacks == 1);
    check(host.sent.size() == 3);
    check(host.sent[0].bus == 0 && host.sent[0].offset == 5);
    check(host.sent[0].is({0x92, 64, 96}));
    check(host.sent[1].offset == 10 && host.sent[1].is({0x82, 64, 64}));
    check(host.sent[2].offset == 12 && host.sent[2].is({0xB3, 123, 0}));

    check(host.render(maxBlock) == noErr);
    check(host.midiCallbacks == 1);

    auto gain = UnitHost<GainPlugin>("Gain");
    check(!gain.hasMidiOut());
};

auto tNoMidiCallback = test("AU/noMidiCallbackNoCrash") = []
{
    auto host = UnitHost<SynthPlugin>("Echo", Start::Open);
    host.collectMidi = false;
    check(host.initialize() == noErr);

    MusicDeviceMIDIEvent(host.unit, 0x90, 60, 100, 0);
    check(host.render(maxBlock) == noErr);
    check(host.plugin().numEvents == 1);
    check(host.sent.empty());
};

auto tReset = test("AU/resetSilencesAHeldNote") = []
{
    auto holding = ScopedStandIn<HoldingSynth>(Module::echoStandIn);
    auto host = UnitHost<HoldingSynth>("Echo");

    MusicDeviceMIDIEvent(host.unit, 0x90, 60, 100, 0);
    check(host.render(maxBlock) == noErr);
    check(host.outputs[0].storage[0][maxBlock - 1] == 1.f);

    check(host.render(maxBlock) == noErr);
    check(host.outputs[0].storage[0][0] == 1.f);

    auto resets = host.plugin().resets;
    check(AudioUnitReset(host.unit, kAudioUnitScope_Global, 0) == noErr);
    check(host.render(maxBlock) == noErr);

    check(host.plugin().resets == resets + 1);
    check(host.outputs[0].storage[0][0] == 0.f);
    check(host.outputs[0].storage[1][maxBlock - 1] == 0.f);
};

auto tResetFirst = test("AU/aResetBeforeAnyRenderIsHarmless") = []
{
    auto holding = ScopedStandIn<HoldingSynth>(Module::echoStandIn);
    auto host = UnitHost<HoldingSynth>("Echo");

    check(AudioUnitReset(host.unit, kAudioUnitScope_Global, 0) == noErr);
    host.outputs[0].fill(-1.f);
    check(host.render(maxBlock) == noErr);
    check(host.outputs[0].storage[0][0] == 0.f);

    MusicDeviceMIDIEvent(host.unit, 0x90, 60, 100, 0);
    check(host.render(maxBlock) == noErr);
    check(host.outputs[0].storage[0][0] == 1.f);
};

struct PropertyLog
{
    int latency = 0;
    int parameterList = 0;
    int bypass = 0;
    int classInfo = 0;
};

void recordProperty(void* refCon,
                    AudioUnit,
                    AudioUnitPropertyID id,
                    AudioUnitScope,
                    AudioUnitElement)
{
    auto& log = *static_cast<PropertyLog*>(refCon);

    if (id == kAudioUnitProperty_Latency)
        ++log.latency;
    else if (id == kAudioUnitProperty_ParameterList)
        ++log.parameterList;
    else if (id == kAudioUnitProperty_BypassEffect)
        ++log.bypass;
    else if (id == kAudioUnitProperty_ClassInfo)
        ++log.classInfo;
}

template <typename P>
double latencySeconds(UnitHost<P>& host)
{
    auto seconds = Float64 {};
    auto size = UInt32 {sizeof(seconds)};
    check(AudioUnitGetProperty(host.unit,
                               kAudioUnitProperty_Latency,
                               kAudioUnitScope_Global,
                               0,
                               &seconds,
                               &size)
          == noErr);
    return seconds;
}

auto tLatency = test("AU/aLatencyChangeIsPostedOnceTheHostHasABaseline") = []
{
    auto latency = ScopedStandIn<LatencyPlugin>(Module::gainStandIn);
    auto host = UnitHost<LatencyPlugin>("Gain");
    auto& plugin = host.plugin();
    auto log = PropertyLog {};

    for (auto id: {kAudioUnitProperty_Latency, kAudioUnitProperty_ParameterList})
        check(AudioUnitAddPropertyListener(host.unit, id, &recordProperty, &log)
              == noErr);

    plugin.params.latency.setValue(480.f);
    plugin.announceLatency();
    check(log.latency == 0);

    check(near(latencySeconds(host), 480.0 / sampleRate));

    plugin.params.latency.setValue(240.f);
    plugin.announceLatency();
    check(log.latency == 1);

    plugin.announceLatency();
    check(log.latency == 1);

    auto& edits = *plugin.hostEditListener();
    edits.beginParameterEdit(0);
    plugin.params.latency.setValue(96.f);
    edits.performParameterEdit(0, plugin.params.latency.getNormalized());
    edits.endParameterEdit(0);
    check(log.latency == 2);

    check(host.hostWrites(host.hostIdOf(plugin.params.latency), 128.f) == noErr);
    check(log.latency == 3);
    check(near(latencySeconds(host), 128.0 / sampleRate));

    auto other = UnitHost<LatencyPlugin>("Gain");
    other.plugin().params.latency.setValue(700.f);
    auto info = CFOwned<CFPropertyListRef>(copyClassInfo(other));

    check(restoreClassInfo(host, info.value) == noErr);
    check(log.latency == 4);
    check(near(latencySeconds(host), 700.0 / sampleRate));

    auto tail = Float64 {-1.0};
    auto size = UInt32 {sizeof(tail)};
    plugin.tail = 4800;
    check(AudioUnitGetProperty(host.unit,
                               kAudioUnitProperty_TailTime,
                               kAudioUnitScope_Global,
                               0,
                               &tail,
                               &size)
          == noErr);
    check(near(tail, 0.1));

    plugin.notifyHostParameterInfoChanged();
    check(log.parameterList == 1);
};

template <typename P>
OSStatus bypassInfo(UnitHost<P>& host, UInt32& size, Boolean& writable)
{
    return AudioUnitGetPropertyInfo(host.unit,
                                    kAudioUnitProperty_BypassEffect,
                                    kAudioUnitScope_Global,
                                    0,
                                    &size,
                                    &writable);
}

template <typename P>
UInt32 readBypass(UnitHost<P>& host)
{
    auto value = UInt32 {7};
    auto size = UInt32 {sizeof(value)};
    check(AudioUnitGetProperty(host.unit,
                               kAudioUnitProperty_BypassEffect,
                               kAudioUnitScope_Global,
                               0,
                               &value,
                               &size)
          == noErr);
    return value;
}

template <typename P>
OSStatus writeBypass(UnitHost<P>& host, UInt32 value)
{
    return AudioUnitSetProperty(host.unit,
                                kAudioUnitProperty_BypassEffect,
                                kAudioUnitScope_Global,
                                0,
                                &value,
                                sizeof(value));
}

template <typename P>
void listenForBypass(UnitHost<P>& host, PropertyLog& log)
{
    check(AudioUnitAddPropertyListener(
              host.unit, kAudioUnitProperty_BypassEffect, &recordProperty, &log)
          == noErr);
}

auto tBypassAnswered = test("AU/bypassIsAnsweredOnlyWithABypassParameter") = []
{
    auto gain = UnitHost<GainPlugin>("Gain");
    auto size = UInt32 {};
    auto writable = Boolean {};

    check(bypassInfo(gain, size, writable) == noErr);
    check(size == sizeof(UInt32));
    check(writable);

    auto echo = UnitHost<SynthPlugin>("Echo");
    check(bypassInfo(echo, size, writable) == kAudioUnitErr_InvalidProperty);
    check(writeBypass(echo, 1) != noErr);
};

auto tBypassReads = test("AU/readingBypassFollowsTheParameter") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& bypass = host.plugin().params.bypass;

    check(readBypass(host) == 0);

    bypass.setValue(1.f);
    check(readBypass(host) == 1);

    bypass.setValue(0.f);
    check(readBypass(host) == 0);
};

auto tBypassWrites = test("AU/writingBypassMovesTheParameterAndItsLane") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& bypass = host.plugin().params.bypass;
    auto bypassId = host.hostIdOf(bypass);
    auto log = GestureLog {};
    auto properties = PropertyLog {};
    listenForBypass(host, properties);

    auto listener = AUEventListenerRef {};
    check(AUEventListenerCreate(&recordGesture,
                                &log,
                                CFRunLoopGetCurrent(),
                                kCFRunLoopDefaultMode,
                                0.0,
                                0.0,
                                &listener)
          == noErr);

    auto event = AudioUnitEvent {};
    event.mEventType = kAudioUnitEvent_ParameterValueChange;
    event.mArgument.mParameter = {host.unit, bypassId, kAudioUnitScope_Global, 0};
    check(AUEventListenerAddEventType(listener, &log, &event) == noErr);

    check(writeBypass(host, 1) == noErr);
    check(bypass.isOn());
    check(host.hostValue(bypassId) == 1.f);
    check(readBypass(host) == 1);
    check(properties.bypass == 1);

    check(pumpUntil([&] { return !log.types.empty(); }));
    check(log.types[0] == kAudioUnitEvent_ParameterValueChange);
    check(log.ids[0] == bypassId);
    check(log.values[0] == 1.f);

    check(host.render(maxBlock) == noErr);
    check(bypass.isOn());
    check(host.hostValue(bypassId) == 1.f);

    check(writeBypass(host, 0) == noErr);
    check(!bypass.isOn());
    check(host.hostValue(bypassId) == 0.f);

    AUListenerDispose(listener);
};

auto tBypassPosted = test("AU/aBypassMoveIsPostedToPropertyListeners") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& params = host.plugin().params;
    auto log = PropertyLog {};
    listenForBypass(host, log);

    check(host.hostWrites(host.hostIdOf(params.bypass), 1.f) == noErr);
    check(log.bypass == 1);

    check(host.hostWrites(host.hostIdOf(params.bypass), 1.f) == noErr);
    check(host.hostWrites(host.hostIdOf(params.gain), -6.f) == noErr);
    check(log.bypass == 1);

    auto& edits = *host.plugin().hostEditListener();
    auto index = host.plugin().parameters().indexOf(&params.bypass);
    edits.beginParameterEdit(index);
    params.bypass.setValue(0.f);
    edits.performParameterEdit(index, params.bypass.getNormalized());
    edits.endParameterEdit(index);
    check(log.bypass == 2);
    check(readBypass(host) == 0);

    edits.beginParameterEdit(0);
    params.gain.setValue(-3.f);
    edits.performParameterEdit(0, params.gain.getNormalized());
    edits.endParameterEdit(0);
    check(log.bypass == 2);
};

auto tBypassHeld = test("AU/aHeldBypassDropsTheWriteAndMirrorsItBack") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& bypass = host.plugin().params.bypass;
    auto bypassId = host.hostIdOf(bypass);
    auto& edits = *host.plugin().hostEditListener();
    auto index = host.plugin().parameters().indexOf(&bypass);
    check(host.render(maxBlock) == noErr);

    edits.beginParameterEdit(index);
    check(writeBypass(host, 1) == noErr);
    check(!bypass.isOn());
    check(readBypass(host) == 0);

    check(host.render(maxBlock) == noErr);
    check(!bypass.isOn());
    check(host.hostValue(bypassId) == 0.f);

    edits.endParameterEdit(index);

    check(writeBypass(host, 1) == noErr);
    check(bypass.isOn());
};

auto tCocoaView = test("AU/theCocoaViewIsAnsweredOnlyWithAView") = []
{
    auto host = UnitHost<GainPlugin>("Gain");

    auto size = UInt32 {};
    auto writable = Boolean {};
    auto answered = AudioUnitGetPropertyInfo(host.unit,
                                             kAudioUnitProperty_CocoaUI,
                                             kAudioUnitScope_Global,
                                             0,
                                             &size,
                                             &writable)
                    == noErr;

#if MAKEASOUND_AU_HAS_VIEW
    check(answered);
    check(size == sizeof(AudioUnitCocoaViewInfo));

    auto info = AudioUnitCocoaViewInfo {};
    check(AudioUnitGetProperty(host.unit,
                               kAudioUnitProperty_CocoaUI,
                               kAudioUnitScope_Global,
                               0,
                               &info,
                               &size)
          == noErr);

    auto className = CFOwned<CFStringRef>(info.mCocoaAUViewClass[0]);
    auto bundle = CFOwned<CFURLRef>(info.mCocoaAUViewBundleLocation);
    check(bundle.value != nullptr);

    auto* factoryClass = objc_getClass(toUtf8(className.value).c_str());
    check(factoryClass != nullptr);

    auto* protocol = objc_getProtocol("AUCocoaUIBase");
    check(protocol != nullptr && class_conformsToProtocol(factoryClass, protocol));
#else
    check(!answered);
#endif

    auto* adapter = static_cast<void*>(nullptr);
    size = sizeof(adapter);
    check(AudioUnitGetProperty(host.unit,
                               AU::adapterProperty,
                               kAudioUnitScope_Global,
                               0,
                               &adapter,
                               &size)
          == noErr);
    check(adapter != nullptr);
};

void pumpFor(int milliseconds)
{
    eacp::Threads::runEventLoopUntil([] { return false; },
                                     eacp::Time::MS {milliseconds});
}

// An event listener on the run loop for every value change of `ids`.
template <typename P>
AUEventListenerRef listenForValues(UnitHost<P>& host,
                                   GestureLog& log,
                                   const std::vector<AudioUnitParameterID>& ids)
{
    auto listener = AUEventListenerRef {};
    check(AUEventListenerCreate(&recordGesture,
                                &log,
                                CFRunLoopGetCurrent(),
                                kCFRunLoopDefaultMode,
                                0.0,
                                0.0,
                                &listener)
          == noErr);

    for (auto id: ids)
    {
        auto event = AudioUnitEvent {};
        event.mEventType = kAudioUnitEvent_ParameterValueChange;
        event.mArgument.mParameter = {host.unit, id, kAudioUnitScope_Global, 0};
        check(AUEventListenerAddEventType(listener, &log, &event) == noErr);
    }

    return listener;
}

auto tContextName = test("AU/theContextNameReachesTheTrackInfo") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& plugin = host.plugin();
    check(!plugin.trackInfo().isValid);

    auto setName = [&](const char* text)
    {
        auto name = CFStringCreateWithCString(nullptr, text, kCFStringEncodingUTF8);
        auto status = AudioUnitSetProperty(host.unit,
                                           kAudioUnitProperty_ContextName,
                                           kAudioUnitScope_Global,
                                           0,
                                           &name,
                                           sizeof(name));
        CFRelease(name);
        return status;
    };

    check(setName("Lead Vocal") == noErr);
    check(plugin.trackInfoChanges == 1);
    check(plugin.trackInfo().isValid);
    check(plugin.trackInfo().name == "Lead Vocal");

    check(setName("Drums") == noErr);
    check(plugin.trackInfoChanges == 2);
    check(plugin.trackInfo().name == "Drums");

    // The base still answers the property with what the host set.
    auto read = CFStringRef {};
    auto size = UInt32 {sizeof(read)};
    check(AudioUnitGetProperty(host.unit,
                               kAudioUnitProperty_ContextName,
                               kAudioUnitScope_Global,
                               0,
                               &read,
                               &size)
          == noErr);
    check(AU::toUtf8(read) == "Drums");
    CFRelease(read);
};

auto tPluginMovePosted = test("AU/aPluginSideMoveReachesAnEventListener") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    auto gainId = host.hostIdOf(gain);
    auto log = GestureLog {};
    auto listener = listenForValues(host, log, {gainId});

    // The render finds the move and raises a bit; the timer posts it.
    gain.setValue(-7.f);
    check(host.render(maxBlock) == noErr);
    check(pumpUntil([&] { return !log.types.empty(); }));
    check(log.types.size() == 1);
    check(log.ids[0] == gainId);
    check(near(log.values[0], -7.f));

    // Once: a further render with nothing moved posts nothing.
    check(host.render(maxBlock) == noErr);
    pumpFor(150);
    check(log.types.size() == 1);

    // A host write is the host's own news.
    check(host.hostWrites(gainId, -2.f) == noErr);
    check(host.render(maxBlock) == noErr);
    pumpFor(150);
    check(log.types.size() == 1);

    AUListenerDispose(listener);
};

auto tHeldMoveNotPosted = test("AU/aHeldParameterIsLeftToItsGesture") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    auto gainId = host.hostIdOf(gain);
    auto log = GestureLog {};
    auto listener = listenForValues(host, log, {gainId});
    auto& edits = *host.plugin().hostEditListener();

    edits.beginParameterEdit(0);
    gain.setValue(-18.f);
    check(host.render(maxBlock) == noErr);
    pumpFor(150);
    check(log.types.empty());
    check(near(host.hostValue(gainId), -18.f));

    edits.performParameterEdit(0, gain.getNormalized());
    edits.endParameterEdit(0);
    check(pumpUntil([&] { return !log.types.empty(); }));
    check(host.render(maxBlock) == noErr);
    pumpFor(150);
    check(log.types.size() == 1);

    AUListenerDispose(listener);
};

auto tBulkPosted = test("AU/manyValuesChangedPostsAnEventPerParameter") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& params = host.plugin().params;
    const auto& list = host.plugin().parameters();
    auto ids = std::vector<AudioUnitParameterID> {};

    for (auto i = 0; i < list.size(); ++i)
        if (list.isHostExposed(i))
            ids.push_back(list.entry(i).hostId);

    auto log = GestureLog {};
    auto listener = listenForValues(host, log, ids);
    auto properties = PropertyLog {};
    listenForBypass(host, properties);

    moveEveryParameter(params);
    host.plugin().notifyHostParameterValuesChanged();

    // Globals() holds them at once, before any render.
    check(near(host.hostValue(host.hostIdOf(params.gain)), -12.f));
    check(near(host.hostValue(host.hostIdOf(params.mode)), 2.f));
    check(properties.bypass == 1);

    check(pumpUntil([&] { return log.types.size() >= ids.size(); }));
    check(
        std::all_of(log.ids.begin(),
                    log.ids.end(),
                    [&](auto id)
                    { return std::find(ids.begin(), ids.end(), id) != ids.end(); }));

    // The next render takes them as the host's and posts nothing more.
    check(host.render(maxBlock) == noErr);
    pumpFor(150);
    check(log.types.size() == ids.size());
    check(near(params.gain.getValue(), -12.f));

    AUListenerDispose(listener);
};

auto tClassInfoPosted = test("AU/markChangedPostsClassInfo") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto log = PropertyLog {};
    check(AudioUnitAddPropertyListener(
              host.unit, kAudioUnitProperty_ClassInfo, &recordProperty, &log)
          == noErr);

    host.plugin().state.preset = "Edited";
    host.plugin().state.markChanged();
    check(log.classInfo == 1);

    // A restore is the host's own doing.
    auto source = UnitHost<GainPlugin>("Gain");
    source.plugin().state.preset = "Other";
    source.plugin().state.markChanged();
    auto info = CFOwned<CFPropertyListRef>(copyClassInfo(source));
    check(restoreClassInfo(host, info.value) == noErr);
    check(pumpUntil([&] { return host.plugin().state.preset == "Other"; }));
    check(log.classInfo == 1);
};

// ClassInfo with its `version` replaced, as an AUAudioUnit's own state has it.
CFOwned<CFPropertyListRef> withVersion(CFPropertyListRef info, SInt32 version)
{
    auto* copy = CFDictionaryCreateMutableCopy(
        kCFAllocatorDefault, 0, static_cast<CFDictionaryRef>(info));
    auto number =
        CFOwned<CFNumberRef>(CFNumberCreate(nullptr, kCFNumberSInt32Type, &version));
    CFDictionarySetValue(copy, CFSTR("version"), number.value);
    return CFOwned<CFPropertyListRef>(copy);
}

auto tVersionOne = test("AU/classInfoOfVersionOneRestoresAsZero") = []
{
    auto source = UnitHost<GainPlugin>("Gain");
    moveEveryParameter(source.plugin().params);
    auto info = CFOwned<CFPropertyListRef>(copyClassInfo(source));

    auto one = withVersion(info.value, 1);
    auto target = UnitHost<GainPlugin>("Gain");
    check(restoreClassInfo(target, one.value) == noErr);
    check(near(target.plugin().params.gain.getValue(), -12.f));
    check(target.plugin().params.mode.getIndex() == 2);

    // The caller's dictionary is left as it was.
    auto* version = static_cast<CFNumberRef>(CFDictionaryGetValue(
        static_cast<CFDictionaryRef>(one.value), CFSTR("version")));
    auto value = SInt32 {};
    CFNumberGetValue(version, kCFNumberSInt32Type, &value);
    check(value == 1);

    auto two = withVersion(info.value, 2);
    auto other = UnitHost<GainPlugin>("Gain");
    check(restoreClassInfo(other, two.value) == kAudioUnitErr_InvalidPropertyValue);
    check(near(other.plugin().params.gain.getValue(), 0.f));
};

// A JUCE build's session, as its AU wrapper saves one.

using JuceFixtures::FixturePlugin;

// The id a JUCE build gave `cutoff`, which is not this build's.
const auto juceCutoffId = Compat::juceStringHash("cutoff") & 0x7fffffffu;

void appendBigEndian(CFMutableDataRef data, UInt32 value)
{
    auto big = CFSwapInt32HostToBig(value);
    CFDataAppendBytes(data, reinterpret_cast<const UInt8*>(&big), sizeof(big));
}

// The SDK's keys of `ours` (a unit under the same codes), the JUCE build's
// parameter data (one global parameter under its own id) and its plugin state
// under jucePluginState, as JUCE's SaveState writes them.
CFOwned<CFPropertyListRef>
    juceClassInfo(CFPropertyListRef ours, std::string_view state, SInt32 version = 0)
{
    auto* copy = CFDictionaryCreateMutableCopy(
        kCFAllocatorDefault, 0, static_cast<CFDictionaryRef>(ours));
    CFDictionaryRemoveValue(copy, CFSTR("MakeASoundState"));

    auto data =
        CFOwned<CFMutableDataRef>(CFDataCreateMutable(kCFAllocatorDefault, 0));
    appendBigEndian(data.value, kAudioUnitScope_Global);
    appendBigEndian(data.value, 0);
    appendBigEndian(data.value, 1);
    appendBigEndian(data.value, juceCutoffId);
    appendBigEndian(data.value, std::bit_cast<UInt32>(440.f));
    CFDictionarySetValue(copy, CFSTR("data"), data.value);

    auto juceState =
        CFOwned<CFDataRef>(CFDataCreate(kCFAllocatorDefault,
                                        reinterpret_cast<const UInt8*>(state.data()),
                                        static_cast<CFIndex>(state.size())));
    CFDictionarySetValue(copy, CFSTR("jucePluginState"), juceState.value);

    auto number =
        CFOwned<CFNumberRef>(CFNumberCreate(nullptr, kCFNumberSInt32Type, &version));
    CFDictionarySetValue(copy, CFSTR("version"), number.value);

    return CFOwned<CFPropertyListRef>(copy);
}

bool hasGlobalParameter(AudioUnit unit, AudioUnitParameterID id)
{
    auto value = AudioUnitParameterValue {};
    return AudioUnitGetParameter(unit, id, kAudioUnitScope_Global, 0, &value)
           == noErr;
}

auto tJuceState = test("AU/aJuceClassInfoLoadsThroughThePluginsReader") = []
{
    auto standIn = ScopedStandIn<FixturePlugin>(Module::gainStandIn);
    auto source = UnitHost<FixturePlugin>("Gain");
    auto ours = CFOwned<CFPropertyListRef>(copyClassInfo(source));
    auto info = juceClassInfo(ours.value, JuceFixtures::blob());

    auto target = UnitHost<FixturePlugin>("Gain");
    check(restoreClassInfo(target, info.value) == noErr);

    auto& plugin = target.plugin();
    check(plugin.legacyLoads == 1);
    check(plugin.origin == ForeignOrigin::JuceAU);
    check(plugin.context == StateContext::Session);
    check(JuceFixtures::holdsFixtureValues(plugin.params));
    check(
        near(target.hostValue(target.hostIdOf(plugin.params.cutoff)), 440.f, 1e-2));

    // The JUCE build's ids never reached Globals(), which must not grow.
    check(!hasGlobalParameter(target.unit, juceCutoffId));

    // The next save is the native document, and it loads as one.
    auto saved = CFOwned<CFPropertyListRef>(copyClassInfo(target));
    auto* dictionary = static_cast<CFDictionaryRef>(saved.value);
    check(CFDictionaryGetValue(dictionary, CFSTR("MakeASoundState")) != nullptr);
    check(CFDictionaryGetValue(dictionary, CFSTR("jucePluginState")) == nullptr);

    auto again = UnitHost<FixturePlugin>("Gain");
    check(restoreClassInfo(again, saved.value) == noErr);
    check(again.plugin().legacyLoads == 0);
    check(JuceFixtures::holdsFixtureValues(again.plugin().params));
};

auto tJuceStateForms =
    test("AU/aJuceClassInfoLoadsFromADocumentAndAtVersionOne") = []
{
    auto standIn = ScopedStandIn<FixturePlugin>(Module::gainStandIn);
    auto source = UnitHost<FixturePlugin>("Gain");
    auto ours = CFOwned<CFPropertyListRef>(copyClassInfo(source));

    // A JUCE AUv3's own state, version 1.
    auto one = juceClassInfo(ours.value, JuceFixtures::blob(), 1);
    auto target = UnitHost<FixturePlugin>("Gain");
    check(restoreClassInfo(target, one.value) == noErr);
    check(JuceFixtures::holdsFixtureValues(target.plugin().params));
    check(!hasGlobalParameter(target.unit, juceCutoffId));

    auto info = juceClassInfo(ours.value, JuceFixtures::blob());
    auto document = UnitHost<FixturePlugin>("Gain");
    check(AudioUnitSetProperty(document.unit,
                               kAudioUnitProperty_ClassInfoFromDocument,
                               kAudioUnitScope_Global,
                               0,
                               &info.value,
                               sizeof(info.value))
          == noErr);
    check(document.plugin().origin == ForeignOrigin::JuceAU);
    check(JuceFixtures::holdsFixtureValues(document.plugin().params));
    check(!hasGlobalParameter(document.unit, juceCutoffId));
};

auto tJuceRefused = test("AU/aJuceClassInfoThePluginRefusesMovesNothing") = []
{
    auto standIn = ScopedStandIn<FixturePlugin>(Module::gainStandIn);
    auto source = UnitHost<FixturePlugin>("Gain");
    auto ours = CFOwned<CFPropertyListRef>(copyClassInfo(source));
    auto info = juceClassInfo(ours.value, JuceFixtures::blob());

    auto target = UnitHost<FixturePlugin>("Gain");
    target.plugin().refuse = true;
    check(restoreClassInfo(target, info.value) == noErr);
    check(target.plugin().legacyLoads == 1);
    check(JuceFixtures::holdsDefaults(target.plugin().params));
    check(near(target.hostValue(target.hostIdOf(target.plugin().params.cutoff)),
               1000.f,
               1e-2));
    check(!hasGlobalParameter(target.unit, juceCutoffId));

    // Another component's codes are refused before the plugin hears of it.
    auto echo = UnitHost<SynthPlugin>("Echo");
    echo.plugin().params.level.setValue(0.8f);
    check(restoreClassInfo(echo, info.value) != noErr);
    check(near(echo.plugin().params.level.getValue(), 0.8f));
};

auto tJuceOffThread =
    test("AU/aJuceClassInfoFromAWorkerLoadsOnTheMessageThread") = []
{
    auto standIn = ScopedStandIn<FixturePlugin>(Module::gainStandIn);
    auto source = UnitHost<FixturePlugin>("Gain");
    auto ours = CFOwned<CFPropertyListRef>(copyClassInfo(source));
    auto info = juceClassInfo(ours.value, JuceFixtures::blob());

    auto target = UnitHost<FixturePlugin>("Gain");
    auto result = OSStatus {-1};
    auto loader =
        std::thread([&] { result = restoreClassInfo(target, info.value); });
    loader.join();

    check(result == noErr);
    check(target.plugin().legacyLoads == 0);
    check(pumpUntil([&] { return target.plugin().legacyLoads == 1; }));
    check(JuceFixtures::holdsFixtureValues(target.plugin().params));

    // Its parameterValuesChanged mirrored the values into Globals().
    check(near(target.hostValue(target.hostIdOf(target.plugin().params.cutoff)),
               440.f,
               1e-2));
    check(!hasGlobalParameter(target.unit, juceCutoffId));
};

// A Tamber Plug build's session, as its AU wrapper saved one.

using TamberPlugFixtures::ChorusPort;
using TamberPlugFixtures::SVFPort;

// A Plug fixture's dictionary under the codes of `ours` (a unit hosting the port
// under the test module's codes); every other key as Plug wrote it.
CFOwned<CFPropertyListRef> plugClassInfo(CFPropertyListRef ours,
                                         std::string_view fixture)
{
    auto xml = CFOwned<CFDataRef>(
        CFDataCreate(kCFAllocatorDefault,
                     reinterpret_cast<const UInt8*>(fixture.data()),
                     static_cast<CFIndex>(fixture.size())));
    auto parsed = CFOwned<CFPropertyListRef>(CFPropertyListCreateWithData(
        kCFAllocatorDefault, xml.value, kCFPropertyListImmutable, nullptr, nullptr));
    check(parsed.value != nullptr);

    auto* copy = CFDictionaryCreateMutableCopy(
        kCFAllocatorDefault, 0, static_cast<CFDictionaryRef>(parsed.value));

    for (auto* key: {CFSTR("type"), CFSTR("subtype"), CFSTR("manufacturer")})
        CFDictionarySetValue(
            copy,
            key,
            CFDictionaryGetValue(static_cast<CFDictionaryRef>(ours), key));

    return CFOwned<CFPropertyListRef>(copy);
}

std::string dataUnder(CFPropertyListRef info, CFStringRef key)
{
    auto* data = static_cast<CFDataRef>(
        CFDictionaryGetValue(static_cast<CFDictionaryRef>(info), key));

    if (data == nullptr)
        return {};

    return {reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
            static_cast<size_t>(CFDataGetLength(data))};
}

// None of Plug's ids, its list indices, reached Globals().
template <typename P>
bool holdsNoPlugIds(UnitHost<P>& host, int count)
{
    const auto& list = host.plugin().parameters();

    for (auto id = 0; id < count; ++id)
    {
        auto ours = false;

        for (auto i = 0; i < list.size(); ++i)
            ours = ours || list.entry(i).hostId == static_cast<uint32_t>(id);

        if (!ours && hasGlobalParameter(host.unit, static_cast<UInt32>(id)))
            return false;
    }

    return true;
}

auto tPlugFixture = test("AU/thePlugFixturesHoldPlugsDocuments") = []
{
    auto standIn = ScopedStandIn<SVFPort>(Module::gainStandIn);
    auto source = UnitHost<SVFPort>("Gain");
    auto ours = CFOwned<CFPropertyListRef>(copyClassInfo(source));

    auto svf = plugClassInfo(ours.value, TamberPlugFixtures::svfClassInfo);
    check(dataUnder(svf.value, CFSTR("TamberPlugState"))
          == TamberPlugFixtures::svfDocument);

    auto chorus = plugClassInfo(ours.value, TamberPlugFixtures::chorusClassInfo);
    check(dataUnder(chorus.value, CFSTR("TamberPlugState"))
          == TamberPlugFixtures::chorusDocument);
};

auto tPlugState = test("AU/aTamberPlugClassInfoLoadsThroughThePluginsReader") = []
{
    auto standIn = ScopedStandIn<SVFPort>(Module::gainStandIn);
    auto source = UnitHost<SVFPort>("Gain");
    auto ours = CFOwned<CFPropertyListRef>(copyClassInfo(source));
    auto info = plugClassInfo(ours.value, TamberPlugFixtures::svfClassInfo);

    auto target = UnitHost<SVFPort>("Gain");
    check(restoreClassInfo(target, info.value) == noErr);

    auto& plugin = target.plugin();
    check(plugin.legacyLoads == 1);
    check(plugin.origin == ForeignOrigin::TamberPlugAU);
    check(plugin.context == StateContext::Session);
    check(TamberPlugFixtures::holdsSVFValues(plugin.params));
    check(near(target.hostValue(target.hostIdOf(plugin.params.highPass.cutoff)),
               0.4f));
    check(target.hostValue(target.hostIdOf(plugin.params.lowPass.enabled)) == 0.f);

    // Plug's `data`, under its own ids, never reached Globals().
    check(holdsNoPlugIds(target, TamberPlugFixtures::svfParameterCount));

    // The next save is the native document, and it loads as one.
    auto saved = CFOwned<CFPropertyListRef>(copyClassInfo(target));
    auto* dictionary = static_cast<CFDictionaryRef>(saved.value);
    check(CFDictionaryGetValue(dictionary, CFSTR("MakeASoundState")) != nullptr);
    check(CFDictionaryGetValue(dictionary, CFSTR("TamberPlugState")) == nullptr);

    auto again = UnitHost<SVFPort>("Gain");
    check(restoreClassInfo(again, saved.value) == noErr);
    check(again.plugin().legacyLoads == 0);
    check(TamberPlugFixtures::holdsSVFValues(again.plugin().params));
};

auto tPlugChoices = test("AU/aTamberPlugClassInfoLoadsChoicesByName") = []
{
    auto standIn = ScopedStandIn<ChorusPort>(Module::gainStandIn);
    auto source = UnitHost<ChorusPort>("Gain");
    auto ours = CFOwned<CFPropertyListRef>(copyClassInfo(source));
    auto info = plugClassInfo(ours.value, TamberPlugFixtures::chorusClassInfo);

    auto target = UnitHost<ChorusPort>("Gain");
    check(restoreClassInfo(target, info.value) == noErr);
    check(target.plugin().legacyLoads == 1);
    check(TamberPlugFixtures::holdsChorusValues(target.plugin().params));
    check(target.hostValue(target.hostIdOf(target.plugin().params.mode)) == 1.f);
    check(holdsNoPlugIds(target, TamberPlugFixtures::chorusParameterCount));

    // The document form, as Logic restores it.
    auto document = UnitHost<ChorusPort>("Gain");
    check(AudioUnitSetProperty(document.unit,
                               kAudioUnitProperty_ClassInfoFromDocument,
                               kAudioUnitScope_Global,
                               0,
                               &info.value,
                               sizeof(info.value))
          == noErr);
    check(document.plugin().origin == ForeignOrigin::TamberPlugAU);
    check(TamberPlugFixtures::holdsChorusValues(document.plugin().params));
};

auto tPlugRefused = test("AU/aTamberPlugClassInfoThePluginRefusesMovesNothing") = []
{
    auto info = CFOwned<CFPropertyListRef> {};

    {
        auto standIn = ScopedStandIn<SVFPort>(Module::gainStandIn);
        auto source = UnitHost<SVFPort>("Gain");
        auto ours = CFOwned<CFPropertyListRef>(copyClassInfo(source));
        auto made = plugClassInfo(ours.value, TamberPlugFixtures::svfClassInfo);
        std::swap(info.value, made.value);

        auto target = UnitHost<SVFPort>("Gain");
        target.plugin().refuse = true;
        check(restoreClassInfo(target, info.value) == noErr);
        check(target.plugin().legacyLoads == 1);
        check(TamberPlugFixtures::holdsSVFDefaults(target.plugin().params));
        check(holdsNoPlugIds(target, TamberPlugFixtures::svfParameterCount));
    }

    // A plugin that never opted in (loadLegacy's default) keeps its values.
    auto unported = UnitHost<GainPlugin>("Gain");
    unported.plugin().params.gain.setValue(-3.f);
    check(restoreClassInfo(unported, info.value) == noErr);
    check(near(unported.plugin().params.gain.getValue(), -3.f));
};

// An effect with no bypass parameter answers BypassEffect through the one the
// wrapper adds, and the host's bypass reaches the audio.
auto tAddedBypass = test("AU/theWrappersBypassAnswersBypassEffect") = []
{
    auto standIn = ScopedStandIn<UnbypassedGainPlugin>(Module::gainStandIn);
    auto host = UnitHost<UnbypassedGainPlugin>("Gain");
    const auto& list = host.plugin().parameters();
    auto bypassId = list.entry(list.bypassIndex()).hostId;
    auto size = UInt32 {};
    auto writable = Boolean {};

    check(bypassId == MakeASound::hostIdFor("__bypass"));
    check(bypassInfo(host, size, writable) == noErr);
    check(writable);
    check(readBypass(host) == 0);

    host.plugin().params.gain.setValue(6.f);
    host.inputs[0].fill(0.5f);

    check(writeBypass(host, 1) == noErr);
    check(readBypass(host) == 1);
    check(host.hostValue(bypassId) == 1.f);

    // Past the 480-sample crossfade.
    for (auto b = 0; b < 10; ++b)
        check(host.render(blockSize) == noErr);

    check(host.renderedSample(0, 0) == 0.5f);
    check(host.renderedSample(1, blockSize - 1) == 0.5f);
};

// A JUCE port under its JUCE build's ids (JucePort.h), through the factory the
// test module builds over a description of its own.

using JucePort::PortPlugin;

struct PublishedInfo
{
    OSStatus status = noErr;
    AudioUnitParameterInfo info {};
    std::string name;
};

PublishedInfo publishedInfo(AudioUnit unit, AudioUnitParameterID id)
{
    auto result = PublishedInfo {};
    auto size = UInt32 {sizeof(result.info)};
    result.status = AudioUnitGetProperty(unit,
                                         kAudioUnitProperty_ParameterInfo,
                                         kAudioUnitScope_Global,
                                         id,
                                         &result.info,
                                         &size);

    if (result.status == noErr)
    {
        result.name = toUtf8(result.info.cfNameString);

        if ((result.info.flags & kAudioUnitParameterFlag_CFNameRelease) != 0)
            CFRelease(result.info.cfNameString);
    }

    return result;
}

std::vector<std::string>
    valueStringsOf(AudioUnit unit, AudioUnitParameterID id, OSStatus& result)
{
    auto strings = CFArrayRef {};
    auto size = UInt32 {sizeof(strings)};
    result = AudioUnitGetProperty(unit,
                                  kAudioUnitProperty_ParameterValueStrings,
                                  kAudioUnitScope_Global,
                                  id,
                                  &strings,
                                  &size);

    auto names = std::vector<std::string> {};

    if (result != noErr || strings == nullptr)
        return names;

    for (auto i = CFIndex {0}; i < CFArrayGetCount(strings); ++i)
        names.push_back(
            toUtf8(static_cast<CFStringRef>(CFArrayGetValueAtIndex(strings, i))));

    CFRelease(strings);
    return names;
}

std::string textForHostValue(AudioUnit unit, AudioUnitParameterID id, float value)
{
    auto request = AudioUnitParameterStringFromValue {};
    request.inParamID = id;
    request.inValue = &value;
    auto size = UInt32 {sizeof(request)};
    check(AudioUnitGetProperty(unit,
                               kAudioUnitProperty_ParameterStringFromValue,
                               kAudioUnitScope_Global,
                               0,
                               &request,
                               &size)
          == noErr);

    auto text = CFOwned<CFStringRef>(request.outString);
    return toUtf8(text.value);
}

float hostValueForText(AudioUnit unit, AudioUnitParameterID id, CFStringRef text)
{
    auto request = AudioUnitParameterValueFromString {};
    request.inParamID = id;
    request.inString = text;
    auto size = UInt32 {sizeof(request)};
    check(AudioUnitGetProperty(unit,
                               kAudioUnitProperty_ParameterValueFromString,
                               kAudioUnitScope_Global,
                               0,
                               &request,
                               &size)
          == noErr);
    return request.outValue;
}

auto tPortType = test("AU/aJucePortRegistersAndDispatchesItsAUType") = []
{
    // The description's auType wins over the category and the layout, in the
    // component info both plists are written from.
    check(portDescriptionFor(IdScheme::JUCE).componentType
          == kAudioUnitType_MusicEffect);
    check(portDescriptionFor(IdScheme::JUCELegacyIndex).componentType
          == kAudioUnitType_Effect);

    // The factory answers that type alone.
    auto other = portDescriptionFor(IdScheme::JUCE);
    other.componentType = kAudioUnitType_Effect;
    check(MakeASoundAUTestPortFactory(&other) == nullptr);

    for (auto scheme: {IdScheme::JUCE, IdScheme::JUCELegacyIndex})
    {
        auto unit = AudioComponentInstance {};
        Module::lastCreated = nullptr;
        check(AudioComponentInstanceNew(portComponentFor(scheme), &unit) == noErr);
        check(Module::lastCreated != nullptr);
        check(Module::lastCreated->name() == "Port");
        check(Module::lastCreated->parameters().identity()
              == policyFor(scheme, {}).parameters);
        AudioComponentInstanceDispose(unit);
    }
};

auto tPortIds = test("AU/aJucePortsParameterIdsAndListAreJuces") = []
{
    auto host = UnitHost<PortPlugin>(portComponentFor(IdScheme::JUCE));
    const auto& p = host.plugin().params;
    const auto& list = host.plugin().parameters();

    check(host.hostIdOf(p.cutoff) == 798419949u);
    check(host.hostIdOf(p.mode) == 3357091u);
    check(host.hostIdOf(p.enabled) == 537889601u);
    check(host.hostIdOf(p.voices) == 1336493441u);
    check(host.hostIdOf(p.env.attack) == 740224584u);

    // JUCE's (hint, id) order over every parameter, the non-automatable one
    // included; the added bypass is in no list.
    auto expected = std::vector<AudioUnitParameterID> {3357091u,
                                                       537889601u,
                                                       740224584u,
                                                       798419949u,
                                                       1336493441u,
                                                       host.hostIdOf(p.quality)};
    check(parameterListOf(host.unit) == expected);

    auto bypass = list.bypassIndex();
    check(bypass >= 0 && list.entry(bypass).addedBypass);
    check(list.entry(bypass).hostId == juceVst3BypassId);
    check(publishedInfo(host.unit, juceVst3BypassId).status
          == kAudioUnitErr_InvalidParameter);
    check(host.hostWrites(juceVst3BypassId, 1.f) == kAudioUnitErr_InvalidParameter);
    check(!hasGlobalParameter(host.unit, juceVst3BypassId));
};

auto tPortRanges = test("AU/aJucePortPublishesJucesRanges") = []
{
    auto host = UnitHost<PortPlugin>(portComponentFor(IdScheme::JUCE));
    const auto& p = host.plugin().params;

    auto always = kAudioUnitParameterFlag_IsReadable
                  | kAudioUnitParameterFlag_IsWritable
                  | kAudioUnitParameterFlag_HasCFNameString
                  | kAudioUnitParameterFlag_ValuesHaveStrings
                  | kAudioUnitParameterFlag_IsHighResolution;

    // Continuous, skew and all: 0..1, the normalized default.
    auto cutoff = publishedInfo(host.unit, host.hostIdOf(p.cutoff));
    check(cutoff.status == noErr && cutoff.name == "Cutoff");
    check(cutoff.info.minValue == 0.f && cutoff.info.maxValue == 1.f);
    check(near(cutoff.info.defaultValue, 0.404752135, 1e-6));
    check(cutoff.info.unit == kAudioUnitParameterUnit_Generic);
    check((cutoff.info.flags & always) == always);
    check((cutoff.info.flags & kAudioUnitParameterFlag_CanRamp) != 0);
    check((cutoff.info.flags & kAudioUnitParameterFlag_NonRealTime) == 0);
    check((cutoff.info.flags & kAudioUnitParameterFlag_DisplayMask) == 0);
    check(near(host.hostValue(host.hostIdOf(p.cutoff)), 0.404752135, 1e-6));

    // An int is continuous to JUCE.
    auto voices = publishedInfo(host.unit, host.hostIdOf(p.voices));
    check(voices.info.minValue == 0.f && voices.info.maxValue == 1.f);
    check(near(voices.info.defaultValue, 0.466666669, 1e-6));
    check(voices.info.unit == kAudioUnitParameterUnit_Generic);

    // A choice of three: 0..2 at 1, Indexed, its names.
    auto mode = publishedInfo(host.unit, host.hostIdOf(p.mode));
    check(mode.info.minValue == 0.f && mode.info.maxValue == 2.f);
    check(mode.info.defaultValue == 1.f);
    check(mode.info.unit == kAudioUnitParameterUnit_Indexed);
    check((mode.info.flags & kAudioUnitParameterFlag_CanRamp) == 0);
    check(host.hostValue(host.hostIdOf(p.mode)) == 1.f);

    auto result = OSStatus {};
    check(valueStringsOf(host.unit, host.hostIdOf(p.mode), result)
          == std::vector<std::string> {"Clean", "Warm", "Hot"});
    check(result == noErr);

    // A bool: 0..1, Boolean.
    auto enabled = publishedInfo(host.unit, host.hostIdOf(p.enabled));
    check(enabled.info.maxValue == 1.f && enabled.info.defaultValue == 1.f);
    check(enabled.info.unit == kAudioUnitParameterUnit_Boolean);
    check(valueStringsOf(host.unit, host.hostIdOf(p.enabled), result).size() == 2);

    // Not automatable: listed all the same, NonRealTime.
    auto quality = publishedInfo(host.unit, host.hostIdOf(p.quality));
    check(quality.status == noErr);
    check((quality.info.flags & kAudioUnitParameterFlag_NonRealTime) != 0);

    // A continuous one has no strings to list.
    valueStringsOf(host.unit, host.hostIdOf(p.cutoff), result);
    check(result != noErr);
};

auto tPortValues =
    test("AU/aJucePortsValuesAreTheNormalizedOnesTimesTheMaximum") = []
{
    auto host = UnitHost<PortPlugin>(portComponentFor(IdScheme::JUCE));
    auto& p = host.plugin().params;
    auto cutoffId = host.hostIdOf(p.cutoff);
    auto modeId = host.hostIdOf(p.mode);
    auto voicesId = host.hostIdOf(p.voices);
    auto qualityId = host.hostIdOf(p.quality);

    check(host.hostWrites(cutoffId, 0.5f) == noErr);
    check(host.hostWrites(modeId, 2.f) == noErr);
    check(host.hostWrites(qualityId, 0.25f) == noErr);
    check(host.render(maxBlock) == noErr);

    check(near(p.cutoff.getValue(), p.cutoff.toPlain(0.5f), 1e-2));
    check(p.mode.getIndex() == 2);
    check(near(p.quality.getValue(), 0.25f));
    check(host.hostValue(cutoffId) == 0.5f);

    // A value the plugin moves reaches Globals() in the same space.
    p.voices.setValue(16.f);
    check(host.render(maxBlock) == noErr);
    check(host.hostValue(voicesId) == 1.f);

    // Text: of the normalized value, and back.
    check(textForHostValue(host.unit, modeId, 0.f) == "Clean");
    check(textForHostValue(host.unit, cutoffId, 0.404752135f)
          == p.cutoff.valueToText(p.cutoff.toPlain(0.404752135f)));
    check(hostValueForText(host.unit, modeId, CFSTR("Hot")) == 2.f);

    auto text = CFOwned<CFStringRef>(
        AU::makeCFString(p.cutoff.valueToText(p.cutoff.toPlain(0.25f))));
    check(near(hostValueForText(host.unit, cutoffId, text.value), 0.25f, 1e-2));
};

auto tPortBypass = test("AU/aJucePortsAddedBypassIsTheBypassPropertyAlone") = []
{
    auto host = UnitHost<PortPlugin>(portComponentFor(IdScheme::JUCE));
    auto& plugin = host.plugin();
    const auto& list = plugin.parameters();
    auto bypass = list.bypassIndex();
    auto size = UInt32 {};
    auto writable = Boolean {};

    check(bypassInfo(host, size, writable) == noErr && writable);
    check(readBypass(host) == 0);

    check(writeBypass(host, 1) == noErr);
    check(readBypass(host) == 1);
    check(JuceFixtures::addedBypassOf(plugin) == 1.f);
    check(!hasGlobalParameter(host.unit, list.entry(bypass).hostId));

    // The editor's edit reaches property listeners, with no parameter event.
    auto log = PropertyLog {};
    listenForBypass(host, log);

    auto& edits = *plugin.hostEditListener();
    edits.beginParameterEdit(bypass);
    list[bypass].setValue(0.f);
    edits.performParameterEdit(bypass, 0.f);
    edits.endParameterEdit(bypass);
    check(log.bypass == 1);
    check(readBypass(host) == 0);

    // A held bypass drops the host's write.
    edits.beginParameterEdit(bypass);
    check(writeBypass(host, 1) == noErr);
    check(readBypass(host) == 0);
    edits.endParameterEdit(bypass);
};

auto tPortLegacy = test("AU/aJucePortWithLegacyIdsIsIndexed") = []
{
    auto host = UnitHost<PortPlugin>(portComponentFor(IdScheme::JUCELegacyIndex));
    auto& p = host.plugin().params;
    const auto& list = host.plugin().parameters();

    // The SDK's own indexed list: every parameter by its index, the added
    // bypass (the last index) left out.
    check(parameterListOf(host.unit)
          == std::vector<AudioUnitParameterID> {0, 1, 2, 3, 4, 5});
    check(host.hostIdOf(p.cutoff) == 0 && host.hostIdOf(p.quality) == 5);
    check(list.entry(list.bypassIndex()).hostId == 6);
    check(host.hostWrites(6, 1.f) != noErr);

    // JUCE's range still, but no value strings under legacy ids.
    auto mode = publishedInfo(host.unit, 1);
    check(mode.status == noErr && mode.name == "Mode");
    check(mode.info.maxValue == 2.f);

    auto result = OSStatus {};
    valueStringsOf(host.unit, 1, result);
    check(result != noErr);

    check(host.hostWrites(1, 2.f) == noErr);
    check(host.hostWrites(0, 0.5f) == noErr);
    check(host.render(maxBlock) == noErr);
    check(p.mode.getIndex() == 2);
    check(near(p.cutoff.getValue(), p.cutoff.toPlain(0.5f), 1e-2));

    // A state round trip keeps every index's value.
    auto saved = CFOwned<CFPropertyListRef>(copyClassInfo(host));
    auto other = UnitHost<PortPlugin>(portComponentFor(IdScheme::JUCELegacyIndex));
    check(restoreClassInfo(other, saved.value) == noErr);
    check(other.plugin().params.mode.getIndex() == 2);
    check(other.hostValue(1) == 2.f);
};

auto tPortJuceState = test("AU/aJuceClassInfoRestoresIntoAJucePort") = []
{
    auto source = UnitHost<PortPlugin>(portComponentFor(IdScheme::JUCE));
    auto ours = CFOwned<CFPropertyListRef>(copyClassInfo(source));
    auto info = juceClassInfo(ours.value, JuceFixtures::blob());

    auto target = UnitHost<PortPlugin>(portComponentFor(IdScheme::JUCE));
    check(restoreClassInfo(target, info.value) == noErr);

    auto& p = target.plugin().params;
    check(target.plugin().legacyLoads == 1);
    check(near(p.cutoff.getValue(), 440.f, 1e-2));
    check(p.mode.getIndex() == 2);
    check(!p.enabled.isOn());
    check(near(p.voices.getValue(), 5.f));
    check(near(p.env.attack.getValue(), 0.25f));

    // Globals() holds them in JUCE's space, under JUCE's ids, which are ours.
    check(near(target.hostValue(798419949u), p.cutoff.toNormalized(440.f), 1e-5));
    check(target.hostValue(3357091u) == 2.f);
    check(target.hostValue(537889601u) == 0.f);
};
} // namespace
