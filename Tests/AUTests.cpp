// The AU adapter hosted in-process through AudioToolbox's C API: component
// types, the factory, channel negotiation, a whole render, parameters and their
// text, the edit gate and gesture events, state through ClassInfo, MIDI in and
// out, Reset, latency and the Cocoa view.

#include "AUTestHost.h"

#include <MakeASound/Plugin/AU/CocoaUI.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <objc/runtime.h>

#include <atomic>
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

    FloatParam latency {"Latency", 0.f, 1000.f, 0.f};
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

auto tParameterList = test("AU/theParameterListIsTheHostIdsInOrder") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    const auto& list = host.plugin().parameters();

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

    for (auto i = 0; i < list.size(); ++i)
        check(ids[i] == list.entry(i).hostId);

    auto free = AudioUnitParameterID {12345};
    check(list.indexOfHostId(free) < 0);
    check(host.hostWrites(free, 0.5f) == kAudioUnitErr_InvalidParameter);
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

        check(info.minValue == param.minValue());
        check(info.maxValue == param.maxValue());
        check(info.defaultValue == param.defaultValue());
        check((info.flags & kAudioUnitParameterFlag_IsReadable) != 0);
        check((info.flags & kAudioUnitParameterFlag_IsWritable) != 0);

        auto hasStrings = &param == &params.mode || &param == &params.bypass;
        check(((info.flags & kAudioUnitParameterFlag_ValuesHaveStrings) != 0)
              == hasStrings);

        auto expectedUnit = &param == &params.mode ? kAudioUnitParameterUnit_Indexed
                            : &param == &params.bypass
                                ? kAudioUnitParameterUnit_Boolean
                                : kAudioUnitParameterUnit_Generic;
        check(info.unit == expectedUnit);

        check(near(host.hostValue(entry.hostId), param.defaultValue()));
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

        auto toText = AudioUnitParameterStringFromValue {};
        toText.inParamID = host.hostIdOf(*param);
        toText.inValue = &plain;
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
        check(near(fromText.outValue, param->textToValue(expected), 1e-2));
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
} // namespace
