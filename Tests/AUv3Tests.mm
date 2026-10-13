// The AUv3 adapter hosted in-process as an app hosts it: the registered subclass,
// buses and channel capabilities, a whole render through renderBlock and through
// the internal block with events, the parameter tree and its text, the edit gate
// and automation gestures, both state forms, MIDI in and out, reset, latency and
// bypass.

#include "AUv3TestHost.h"
#include "JuceFixtures.h"
#include "TamberPlugFixtures.h"

#include <MakeASound/Plugin/AUv3/Parameters.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

using namespace nano;
using namespace TestPlugins;
using namespace AUv3Host;

// Counts the KVO notifications it receives, per key.
@interface MakeASoundAUv3TestKeyObserver : NSObject
@property(nonatomic, strong) NSCountedSet<NSString*>* counts;
@end

@implementation MakeASoundAUv3TestKeyObserver
- (instancetype)init
{
    self = [super init];
    _counts = [NSCountedSet set];
    return self;
}

- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context
{
    [_counts addObject:keyPath];
}
@end

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

bool near(double a, double b, double tolerance = 1e-4)
{
    return std::abs(a - b) < tolerance;
}

template <typename Predicate>
bool pumpUntil(Predicate ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {5000});
}

void pumpFor(int milliseconds)
{
    eacp::Threads::runEventLoopFor(eacp::Time::MS {milliseconds});
}

NSString* toNSString(std::string_view text)
{
    return [[NSString alloc] initWithBytes:text.data()
                                    length:text.size()
                                  encoding:NSUTF8StringEncoding];
}

std::string toUtf8(NSString* text)
{
    return text != nil ? std::string(text.UTF8String) : std::string {};
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

// Observes `key` on the unit while it lives.
struct ScopedKeyObserver
{
    ScopedKeyObserver(AUAudioUnit* unitToWatch, NSString* keyToWatch)
        : unit(unitToWatch)
        , key(keyToWatch)
    {
        [unit addObserver:observer forKeyPath:key options:0 context:nullptr];
    }

    ~ScopedKeyObserver() { [unit removeObserver:observer forKeyPath:key]; }

    ScopedKeyObserver(const ScopedKeyObserver&) = delete;
    ScopedKeyObserver& operator=(const ScopedKeyObserver&) = delete;

    int count() const
    {
        return static_cast<int>([observer.counts countForObject:key]);
    }

    AUAudioUnit* unit;
    NSString* key;
    MakeASoundAUv3TestKeyObserver* observer = [MakeASoundAUv3TestKeyObserver new];
};

template <typename P>
void scheduleMidi(UnitHost<P>& host,
                  int offset,
                  std::initializer_list<uint8_t> bytes)
{
    auto data = std::vector<uint8_t>(bytes);
    host.unit.scheduleMIDIEventBlock(static_cast<AUEventSampleTime>(host.sampleTime)
                                         + offset,
                                     0,
                                     static_cast<NSInteger>(data.size()),
                                     data.data());
}

auto tRegistered = test("AUv3/aRegisteredSubclassInstantiatesInProcess") = []
{
    // A type the v2 suite never registers, so the lookup can only find ours.
    auto desc = descriptionFor("Echo");
    desc.componentType = kAudioUnitType_MusicEffect;
    registerSubclass(desc);

    struct Result
    {
        AUAudioUnit* unit = nil;
        bool done = false;
    };

    auto result = Result {};
    auto* into = &result;
    Module::lastCreated = nullptr;

    [AUAudioUnit instantiateWithComponentDescription:desc
                                             options:inProcessOption()
                                   completionHandler:^(AUAudioUnit* unit, NSError*) {
                                     into->unit = unit;
                                     into->done = true;
                                   }];

    check(pumpUntil([&] { return result.done; }));
    auto* created = result.unit;
    check(created != nil);
    check([created isKindOfClass:AUv3::audioUnitClass()]);
    check(Module::lastCreated != nullptr);
    check(Module::lastCreated->format() == PluginFormat::AUv3);
    check(Module::lastCreated->name() == "Synth");

    result.unit = nil;
};

auto tUnknown = test("AUv3/aCodeTheModuleDoesNotShipInstantiatesNothing") = []
{
    auto unknown = descriptionFor("Gain");
    unknown.componentSubType = FourCC("Nope").toUint32();

    auto otherMaker = descriptionFor("Gain");
    otherMaker.componentManufacturer = FourCC("Othr").toUint32();

    for (const auto& desc: {unknown, otherMaker})
    {
        auto* error = static_cast<NSError*>(nil);
        check(instantiate(desc, &error) == nil);
        check(error != nil && error.code == kAudioUnitErr_InvalidProperty);
    }
};

auto tBuses = test("AUv3/busesFollowTheLayout") = []
{
    auto gain = UnitHost<GainPlugin>("Gain", Start::Open);
    check(gain.unit.inputBusses.count == 1);
    check(gain.unit.outputBusses.count == 1);

    for (auto input: {true, false})
    {
        auto* format = gain.bus(input, 0).format;
        check(format.channelCount == 2);
        check(format.commonFormat == AVAudioPCMFormatFloat32);
        check(!format.isInterleaved);
    }

    const auto layout = gain.plugin().getBusLayout();
    check(toUtf8(gain.bus(true, 0).name) == layout.inputs[0].name);
    check(gain.unit.MIDIOutputNames.count == 0);

    auto echo = UnitHost<SynthPlugin>("Echo", Start::Open);
    check(echo.unit.inputBusses.count == 0);
    check(echo.unit.outputBusses.count == 1);
    check([echo.unit.MIDIOutputNames isEqualToArray:@[ @"MIDI Out" ]]);
};

auto tCapabilities = test("AUv3/channelCapabilitiesAreWhatThePluginAccepts") = []
{
    auto gain = UnitHost<GainPlugin>("Gain", Start::Open);
    check([gain.unit.channelCapabilities isEqualToArray:@[ @1, @1, @2, @2 ]]);

    auto echo = UnitHost<SynthPlugin>("Echo", Start::Open);
    check([echo.unit.channelCapabilities isEqualToArray:@[ @0, @2 ]]);
};

auto tMono = test("AUv3/monoIsReachedOneBusAtATime") = []
{
    auto host = UnitHost<GainPlugin>("Gain", Start::Open);

    check(!host.setChannels(true, 0, 3));
    check(host.setChannels(true, 0, 1));
    check(host.setChannels(false, 0, 1));
    check(host.allocate());

    const auto& layout = host.plugin().spec.layout;
    check(layout.getMainInputChannels() == 1);
    check(layout.getMainOutputChannels() == 1);

    fillRamp(host.inputs[0]);
    check(host.render(maxBlock) == noErr);
    check(host.outputs[0].storage[0][10] == host.inputs[0].storage[0][10]);

    // Never while the render resources are allocated.
    check(!host.setChannels(true, 0, 2));
};

auto tRefusedPair = test("AUv3/aRefusedPairFailsAllocation") = []
{
    auto host = UnitHost<GainPlugin>("Gain", Start::Open);

    check(host.setChannels(true, 0, 1));
    check(!host.allocate());
    check(host.plugin().spec.sampleRate == 0);

    check(host.setChannels(false, 0, 1));
    check(host.allocate());

    auto echo = UnitHost<SynthPlugin>("Echo", Start::Open);
    check(!echo.setChannels(false, 0, 1));
};

auto tPrepare = test("AUv3/allocatingPreparesThePlugin") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& plugin = host.plugin();

    check(plugin.spec.sampleRate == 48000);
    check(plugin.spec.maxBlockSize == maxBlock);
    check(plugin.spec.layout == BusLayout::stereoInOut());
    check(plugin.formatAtConstruction == PluginFormat::Unknown);
    check(plugin.formatAtPrepare == PluginFormat::AUv3);
    check(plugin.resets == 1);

    check(host.allocate(44100.0, 32));
    check(plugin.spec.sampleRate == 44100);
    check(plugin.spec.maxBlockSize == 32);
};

auto tUnity = test("AUv3/aBlockAtUnityPassesTheInput") = []
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

auto tNullData = test("AUv3/nullOutputDataIsGivenTheUnitsOwnBuffers") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    fillRamp(host.inputs[0]);

    check(host.render(maxBlock, false) == noErr);
    check(host.rendered->mBuffers[0].mData != nullptr);
    check(host.rendered->mBuffers[0].mData != host.outputs[0].storage[0].data());

    for (auto s = 0; s < maxBlock; ++s)
        check(host.renderedSample(1, s) == host.inputs[0].storage[1][s]);
};

auto tRenderLimits = test("AUv3/aRenderOutsideTheContractIsRefused") = []
{
    auto host = UnitHost<GainPlugin>("Gain", Start::Open);
    host.outputs.emplace_back(2);
    host.internal = host.unit.internalRenderBlock;

    check(host.renderEvents(maxBlock, nullptr) == kAudioUnitErr_Uninitialized);
    check(host.plugin().processed == 0);

    check(host.allocate());
    check(host.render(maxBlock + 1) == kAudioUnitErr_TooManyFramesToProcess);
    check(host.plugin().processed == 0);

    check(host.render(0) == noErr);
};

auto tUnconnected = test("AUv3/anUnconnectedInputRendersSilence") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    host.connectInputs = false;

    host.outputs[0].fill(1.f);
    check(host.render(maxBlock) == noErr);
    check(host.plugin().processed == 1);

    for (const auto& channel: host.outputs[0].storage)
        for (auto s = 0; s < maxBlock; ++s)
            check(channel[s] == 0.f);
};

auto tPlayhead = test("AUv3/thePlayheadComesFromTheHostsBlocks") = []
{
    auto host = UnitHost<GainPlugin>("Gain", Start::Open);

    host.unit.transportStateBlock = ^BOOL(AUHostTransportStateFlags* flags,
                                          double* position,
                                          double* cycleStart,
                                          double* cycleEnd) {
      *flags = AUHostTransportStateMoving | AUHostTransportStateCycling;
      *position = 96000.0;
      *cycleStart = 4.0;
      *cycleEnd = 8.0;
      return YES;
    };

    host.unit.musicalContextBlock = ^BOOL(double* tempo,
                                          double* numerator,
                                          NSInteger* denominator,
                                          double* beat,
                                          NSInteger* toNextBeat,
                                          double* downbeat) {
      *tempo = 90.0;
      *numerator = 3.0;
      *denominator = 4;
      *beat = 6.5;
      *toNextBeat = 0;
      *downbeat = 6.0;
      return YES;
    };

    check(host.allocate());
    host.sampleTime = 4800.0;
    check(host.render(maxBlock) == noErr);

    const auto& playhead = host.plugin().playhead;
    check(playhead.isValid && playhead.isPlaying && playhead.isLooping);
    check(playhead.sampleTime == 96000);
    check(playhead.bpm == 90.0);
    check(playhead.timeSignature.numerator == 3);
    check(playhead.ppqPosition == 6.5 && playhead.barStartPpq == 6.0);
    check(playhead.loopStartPpq == 4.0 && playhead.loopEndPpq == 8.0);
};

auto tPlayheadFallback = test("AUv3/withNoTransportThePlayheadIsTheStreamClock") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    host.sampleTime = 4800.0;

    check(host.render(maxBlock) == noErr);
    check(!host.plugin().playhead.isValid);
    check(host.plugin().playhead.sampleTime == 4800);
};

auto tTwoOutputs =
    test("AUv3/everyOutputBusCarriesItsBlockWhicheverRendersFirst") = []
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

    check(host.render(maxBlock, false, 1) == noErr);
    check(host.renderedSample(0, 0) == 0.75f);
};

auto tTree = test("AUv3/theParameterTreeMirrorsTheList") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    const auto& list = host.plugin().parameters();
    const auto& params = host.plugin().params;
    auto* tree = host.unit.parameterTree;

    check(static_cast<int>(tree.allParameters.count) == list.size());

    for (auto i = 0; i < list.size(); ++i)
    {
        const auto& entry = list.entry(i);
        const auto& param = list[i];
        auto* node = tree.allParameters[static_cast<NSUInteger>(i)];

        check(node.address == entry.hostId);
        check(toUtf8(node.identifier) == entry.id);
        check(toUtf8(node.displayName) == entry.displayName);
        // The v2 info's space: the cutoff's skew is no display flag's, so 0..1.
        auto normalized = &param == &params.filter.cutoff;
        check(node.minValue == (normalized ? 0.f : param.minValue()));
        check(node.maxValue == (normalized ? 1.f : param.maxValue()));
        check(near(node.value,
                   normalized ? param.toNormalized(param.defaultValue())
                              : param.defaultValue()));
        check((node.flags & kAudioUnitParameterFlag_IsReadable) != 0);
        check((node.flags & kAudioUnitParameterFlag_IsWritable) != 0);
        check((node.flags & kAudioUnitParameterFlag_IsHighResolution) != 0);

        auto hasStrings = &param == &params.mode || &param == &params.bypass;
        check(((node.flags & kAudioUnitParameterFlag_ValuesHaveStrings) != 0)
              == hasStrings);
        check((node.valueStrings != nil) == hasStrings);

        auto expectedUnit = &param == &params.mode ? kAudioUnitParameterUnit_Indexed
                            : &param == &params.bypass
                                ? kAudioUnitParameterUnit_Boolean
                                : kAudioUnitParameterUnit_Generic;
        check(node.unit == expectedUnit);
    }

    check([host.parameterFor(params.mode).valueStrings
        isEqualToArray:@[ @"Clean", @"Warm", @"Hot" ]]);

    // Each named group is a group of its own, its identifier its id path.
    check(tree.children.count == 5);
    auto* filter = static_cast<AUParameterGroup*>(tree.children[1]);
    check([filter isKindOfClass:[AUParameterGroup class]]);
    check([filter.identifier isEqualToString:@"Filter"]);
    check([filter.displayName isEqualToString:@"Filter"]);
    check(filter.children.count == 1);
    check([filter.children[0].identifier isEqualToString:@"Filter/Cutoff"]);
};

struct NestedTreeParams : ParameterGroup
{
    struct Osc : ParameterGroup
    {
        Osc()
            : ParameterGroup("Osc")
        {
            add(pitch);
        }

        FloatParam pitch {"Pitch"};
    };

    struct Voice : ParameterGroup
    {
        Voice()
            : ParameterGroup("Voice", {.id = "voice"})
        {
            add(osc, level, hidden);
        }

        Osc osc;
        FloatParam level {"Level"};
        FloatParam hidden {"Hidden", 0.f, 1.f, 0.f, {.automatable = false}};
    };

    NestedTreeParams() { add(gain, voice, pan); }

    FloatParam gain {"Gain"};
    Voice voice;
    FloatParam pan {"Pan"};
};

auto tNested = test("AUv3/nestedGroupsNestInTheTree") = []
{
    auto params = NestedTreeParams {};
    auto list = ParameterList {params};
    auto* tree = AUv3::makeParameterTree(list);

    // gain, Voice[Osc[pitch], level], pan; the hidden one is not the host's.
    check(tree.children.count == 3);
    check(tree.allParameters.count == 4);

    auto* voice = static_cast<AUParameterGroup*>(tree.children[1]);
    check([voice.identifier isEqualToString:@"voice"]);
    check([voice.displayName isEqualToString:@"Voice"]);
    check(voice.children.count == 2);

    auto* osc = static_cast<AUParameterGroup*>(voice.children[0]);
    check([osc.identifier isEqualToString:@"voice/Osc"]);
    check(osc.children.count == 1);
    check([osc.children[0].identifier isEqualToString:@"voice/Osc/Pitch"]);

    check([voice.children[1].identifier isEqualToString:@"voice/Level"]);
    check([tree.children[2].identifier isEqualToString:@"Pan"]);
};

auto tText = test("AUv3/textAndValuesGoThroughTheParameter") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& params = host.plugin().params;

    for (auto* param: std::initializer_list<Parameter*> {
             &params.gain, &params.filter.cutoff, &params.mode})
    {
        // A tree value is in the v2 info's space, 0..1 for the skewed cutoff.
        auto normalized = param == &params.filter.cutoff;
        auto inTree = [&](float plain)
        { return normalized ? param->toNormalized(plain) : plain; };

        auto* node = host.parameterFor(*param);
        auto plain = param->toPlain(0.4f);
        auto value = inTree(plain);
        auto expected = param->valueToText(plain);

        auto* text = [node stringFromValue:&value];
        check(toUtf8(text) == expected);
        check(near([node valueFromString:text],
                   inTree(param->textToValue(expected)),
                   1e-2));
    }

    // A tree write and a plugin move cross in that space too.
    auto& cutoff = params.filter.cutoff;
    host.parameterFor(cutoff).value = 0.3f;
    check(cutoff.getValue() == cutoff.toPlain(0.3f));

    params.gain.setValue(-3.f);
    check(toUtf8([host.parameterFor(params.gain) stringFromValue:nullptr])
          == params.gain.valueToText(-3.f));

    params.width.setValue(0.7f);
    check(near([host.parameterFor(params.width) valueFromString:@"nan"], 0.7f));
};

auto tTreeWrite = test("AUv3/aTreeWriteReachesThePluginAndTheNextBlock") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    fillRamp(host.inputs[0]);

    host.parameterFor(gain).value = -6.f;
    check(near(gain.getValue(), -6.f));

    check(host.render(maxBlock) == noErr);
    check(near(host.outputs[0].storage[0][20],
               host.inputs[0].storage[0][20] * gain.gain()));
};

auto tScheduled =
    test("AUv3/aScheduledParameterLandsBeforeTheBlockAndInTheTree") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    auto* node = host.parameterFor(gain);
    fillRamp(host.inputs[0]);

    host.unit.scheduleParameterBlock(
        AUEventSampleTimeImmediate, 0, node.address, -9.f);
    check(host.render(maxBlock) == noErr);
    check(near(gain.getValue(), -9.f));
    check(near(host.outputs[0].storage[0][1],
               host.inputs[0].storage[0][1] * gain.gain()));

    // The render thread wrote the plugin, and the tree learns it off that thread.
    check(pumpUntil([&] { return near(node.value, -9.f); }));

    auto events = EventList {};
    events.parameter(
        node.address, -3.f, static_cast<AUEventSampleTime>(host.sampleTime));
    check(host.renderEvents(maxBlock, events.head()) == noErr);
    check(near(gain.getValue(), -3.f));
};

auto tOffGrid = test("AUv3/aTreeWriteIsKeptAsTheTreeStoredIt") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& params = host.plugin().params;
    auto* mode = host.parameterFor(params.mode);
    auto* gain = host.parameterFor(params.gain);

    // An indexed parameter snaps in the tree itself, before the plugin sees it.
    mode.value = 1.4f;
    gain.value = -6.123f;
    check(mode.value == 1.f);
    check(params.mode.getIndex() == 1);

    check(host.render(maxBlock) == noErr);
    pumpFor(100);
    check(mode.value == 1.f);
    check(gain.value == -6.123f);
    check(params.mode.getIndex() == 1);
};

auto tContextName = test("AUv3/theContextNameReachesTheTrackInfo") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& plugin = host.plugin();
    check(!plugin.trackInfo().isValid);

    host.unit.contextName = @"Lead Vocal";
    check(plugin.trackInfoChanges == 1);
    check(plugin.trackInfo().isValid);
    check(plugin.trackInfo().name == "Lead Vocal");
    check([host.unit.contextName isEqualToString:@"Lead Vocal"]);

    host.unit.contextName = nil;
    check(plugin.trackInfoChanges == 2);
    check(plugin.trackInfo().isValid);
    check(plugin.trackInfo().name.empty());
};

auto tPluginMoves = test("AUv3/aValueThePluginMovesReachesTheTree") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    auto* node = host.parameterFor(gain);

    gain.setValue(-9.f);
    check(pumpUntil([&] { return near(node.value, -9.f); }));

    node.value = -4.f;
    check(host.render(maxBlock) == noErr);
    check(near(gain.getValue(), -4.f));
};

auto tBulkValues = test("AUv3/manyValuesChangedReachTheTreeAtOnce") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& params = host.plugin().params;
    auto* gain = host.parameterFor(params.gain);
    auto* mode = host.parameterFor(params.mode);

    // No tick in between: the call itself syncs the tree.
    params.gain.setValue(-14.f);
    params.mode.setValue(2.f);
    host.plugin().notifyHostParameterValuesChanged();
    check(near(gain.value, -14.f));
    check(mode.value == 2.f);

    // An AUAudioUnit has no dirty signal; marking the state changed is harmless.
    host.plugin().state.preset = "Edited";
    host.plugin().state.markChanged();
    check(host.render(maxBlock) == noErr);
};

auto tEditGate = test("AUv3/aHeldParameterDropsTheTreeWrite") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    auto* node = host.parameterFor(gain);
    auto& listener = *host.plugin().hostEditListener();

    listener.beginParameterEdit(0);
    gain.setValue(-20.f);
    listener.performParameterEdit(0, gain.getNormalized());
    check(near(node.value, -20.f));

    node.value = -3.f;
    check(near(gain.getValue(), -20.f));

    auto events = EventList {};
    events.parameter(
        node.address, -2.f, static_cast<AUEventSampleTime>(host.sampleTime));
    check(host.renderEvents(maxBlock, events.head()) == noErr);
    check(near(gain.getValue(), -20.f));

    // The dropped write is undone in the tree.
    check(pumpUntil([&] { return near(node.value, -20.f); }));

    listener.endParameterEdit(0);

    node.value = -5.f;
    check(near(gain.getValue(), -5.f));
};

auto tGestures = test("AUv3/gesturesReachAnAutomationObserver") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    auto* tree = host.unit.parameterTree;
    auto address = host.parameterFor(gain).address;

    struct Log
    {
        std::vector<AUParameterAutomationEventType> types;
        std::vector<AUValue> values;
        std::vector<AUParameterAddress> addresses;
    };

    auto log = Log {};
    auto* into = &log;
    auto& types = log.types;
    auto& values = log.values;
    auto& addresses = log.addresses;

    auto token =
        [tree tokenByAddingParameterAutomationObserver:^(
                  NSInteger count, const AUParameterAutomationEvent* events) {
          for (auto i = 0; i < count; ++i)
          {
              into->types.push_back(events[i].eventType);
              into->values.push_back(events[i].value);
              into->addresses.push_back(events[i].address);
          }
        }];

    auto& edits = *host.plugin().hostEditListener();
    edits.beginParameterEdit(0);
    gain.setValue(-15.f);
    edits.performParameterEdit(0, gain.getNormalized());
    edits.endParameterEdit(0);

    check(pumpUntil([&] { return types.size() >= 3; }));
    [tree removeParameterObserver:token];

    check(types.size() == 3);
    check(types[0] == AUParameterAutomationEventTypeTouch);
    check(types[1] == AUParameterAutomationEventTypeValue);
    check(types[2] == AUParameterAutomationEventTypeRelease);
    check(std::all_of(
        addresses.begin(), addresses.end(), [&](auto a) { return a == address; }));
    check(near(values[1], -15.f));
};

auto tStateForms = test("AUv3/fullStateIsAPresetAndTheDocumentsASession") = []
{
    auto source = UnitHost<GainPlugin>("Gain");
    moveEveryParameter(source.plugin().params);
    source.plugin().state.preset = "Saved";
    source.plugin().state.markChanged();
    pumpUntil([&] { return source.plugin().isStateSnapshotCurrent(); });

    auto* preset = source.unit.fullState;
    auto expected = descriptionFor("Gain");
    check([preset[@"type"] unsignedIntValue] == expected.componentType);
    check([preset[@"subtype"] unsignedIntValue] == FourCC("Gain").toUint32());
    check([preset[@"manufacturer"] unsignedIntValue] == FourCC("MaSo").toUint32());
    // The saved-state format's version, which the AudioUnitSDK reads only at 0.
    check([preset[@"version"] isKindOfClass:[NSNumber class]]);
    check([preset[@"version"] intValue] == 0);
    check([source.unit.fullStateForDocument[@"version"] intValue] == 0);
    check([preset[@"MakeASoundState"] isKindOfClass:[NSData class]]);

    auto target = UnitHost<GainPlugin>("Gain");
    auto& params = target.plugin().params;
    params.width.setValue(0.3f);
    target.unit.fullState = preset;

    check(near(params.gain.getValue(), -12.f));
    check(near(params.filter.cutoff.getValue(), 440.f, 1e-2));
    check(params.mode.getIndex() == 2);
    check(params.bypass.isOn());
    check(pumpUntil([&] { return target.plugin().state.preset == "Saved"; }));

    // A preset carries no session-only parameter.
    check(near(params.width.getValue(), 0.3f));

    // The tree follows the load at once.
    check(near(target.parameterFor(params.gain).value, -12.f));
    check(near(target.parameterFor(params.mode).value, 2.f));

    target.unit.fullStateForDocument = source.unit.fullStateForDocument;
    check(near(params.width.getValue(), 0.9f));
    check(near(target.parameterFor(params.width).value, 0.9f));
};

auto tStateInserted = test("AUv3/aParameterInsertedMidListKeepsTheOthers") = []
{
    auto source = UnitHost<GainPlugin>("Gain");
    moveEveryParameter(source.plugin().params);
    auto* document = source.unit.fullStateForDocument;

    auto newer = ScopedStandIn<NewerGainPlugin>(Module::gainStandIn);
    auto target = UnitHost<NewerGainPlugin>("Gain");
    target.unit.fullStateForDocument = document;

    const auto& params = target.plugin().params;
    check(near(params.gain.getValue(), -12.f));
    check(near(params.drive.getValue(), 0.25f));
    check(near(params.filter.cutoff.getValue(), 440.f, 1e-2));
    check(params.mode.getIndex() == 2);
    check(params.bypass.isOn());
    check(near(target.parameterFor(params.drive).value, 0.25f));
};

auto tStateOtherSubtype = test("AUv3/anotherComponentsStateIsRefused") = []
{
    auto gain = UnitHost<GainPlugin>("Gain");
    auto* state = gain.unit.fullStateForDocument;

    auto echo = UnitHost<SynthPlugin>("Echo");
    echo.plugin().params.level.setValue(0.8f);
    echo.unit.fullStateForDocument = state;
    check(near(echo.plugin().params.level.getValue(), 0.8f));

    echo.unit.fullState = @{@"MakeASoundState" : @42};
    check(near(echo.plugin().params.level.getValue(), 0.8f));
};

auto tSaveOffThread =
    test("AUv3/aSaveFromAWorkerReturnsWhileTheMainThreadSpins") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    host.plugin().params.gain.setValue(-6.f);

    auto* unit = host.unit;
    auto* state = static_cast<NSDictionary<NSString*, id>*>(nil);
    auto done = std::atomic<bool> {false};

    auto saver = std::thread(
        [&]
        {
            state = unit.fullStateForDocument;
            done = true;
        });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (!done && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    check(done.load());
    saver.join();
    check(state != nil);

    auto target = UnitHost<GainPlugin>("Gain");
    target.unit.fullStateForDocument = state;
    check(near(target.plugin().params.gain.getValue(), -6.f));
};

// A JUCE build's state, as its AUv3 saves one: the base class's dictionary plus
// jucePluginState, here over the codes `ours` carries.

using JuceFixtures::FixturePlugin;

NSDictionary<NSString*, id>* juceFullState(NSDictionary<NSString*, id>* ours)
{
    auto* state = [NSMutableDictionary<NSString*, id> dictionaryWithDictionary:ours];
    [state removeObjectForKey:@"MakeASoundState"];

    auto bytes = JuceFixtures::blob();
    state[@"jucePluginState"] = [NSData dataWithBytes:bytes.data()
                                               length:bytes.size()];
    return state;
}

auto tJuceState = test("AUv3/aJuceStateLoadsThroughThePluginsReader") = []
{
    auto standIn = ScopedStandIn<FixturePlugin>(Module::gainStandIn);
    auto source = UnitHost<FixturePlugin>("Gain");
    auto* juce = juceFullState(source.unit.fullStateForDocument);

    auto target = UnitHost<FixturePlugin>("Gain");
    target.unit.fullStateForDocument = juce;

    auto& plugin = target.plugin();
    check(plugin.legacyLoads == 1);
    check(plugin.origin == ForeignOrigin::JuceAUv3);
    check(plugin.context == StateContext::Session);
    check(JuceFixtures::holdsFixtureValues(plugin.params));

    // The tree follows the load at once.
    check(near(target.parameterFor(plugin.params.cutoff).value, 440.f, 1e-2));
    check(near(target.parameterFor(plugin.params.mode).value, 2.f));

    // The next save is the native document, and it loads as one.
    auto* saved = target.unit.fullStateForDocument;
    check([saved[@"MakeASoundState"] isKindOfClass:[NSData class]]);
    check(saved[@"jucePluginState"] == nil);

    auto again = UnitHost<FixturePlugin>("Gain");
    again.unit.fullStateForDocument = saved;
    check(again.plugin().legacyLoads == 0);
    check(JuceFixtures::holdsFixtureValues(again.plugin().params));

    // fullState is a preset, for JUCE's state as for ours.
    auto preset = UnitHost<FixturePlugin>("Gain");
    preset.unit.fullState = juce;
    check(preset.plugin().context == StateContext::Preset);
    check(JuceFixtures::holdsFixtureValues(preset.plugin().params));
};

auto tJuceRefused = test("AUv3/aJuceStateThePluginRefusesMovesNothing") = []
{
    auto standIn = ScopedStandIn<FixturePlugin>(Module::gainStandIn);
    auto source = UnitHost<FixturePlugin>("Gain");
    auto* juce = juceFullState(source.unit.fullStateForDocument);

    auto target = UnitHost<FixturePlugin>("Gain");
    target.plugin().refuse = true;
    target.unit.fullStateForDocument = juce;
    check(target.plugin().legacyLoads == 1);
    check(JuceFixtures::holdsDefaults(target.plugin().params));
    check(near(target.parameterFor(target.plugin().params.cutoff).value, 1000.f));

    // Another component's codes never reach a plugin.
    auto echo = UnitHost<SynthPlugin>("Echo");
    echo.plugin().params.level.setValue(0.8f);
    echo.unit.fullStateForDocument = juce;
    check(near(echo.plugin().params.level.getValue(), 0.8f));

    // Nor does an empty one.
    auto* empty = [NSMutableDictionary<NSString*, id> dictionaryWithDictionary:juce];
    empty[@"jucePluginState"] = [NSData data];
    auto other = UnitHost<FixturePlugin>("Gain");
    other.unit.fullStateForDocument = empty;
    check(other.plugin().legacyLoads == 0);
};

// A Tamber Plug build's AU v2 ClassInfo, which a Mac host hands the AUv3 standing
// in for it, here over the codes `ours` carries.
NSDictionary<NSString*, id>* plugFullState(NSDictionary<NSString*, id>* ours,
                                           std::string_view fixture)
{
    auto* xml = [NSData dataWithBytes:fixture.data() length:fixture.size()];
    NSDictionary* parsed = [NSPropertyListSerialization propertyListWithData:xml
                                                                     options:0
                                                                      format:nil
                                                                       error:nil];
    auto* state = [NSMutableDictionary<NSString*, id> dictionaryWithDictionary:parsed];

    for (NSString* key in @[ @"type", @"subtype", @"manufacturer" ])
        state[key] = ours[key];

    return state;
}

auto tPlugState = test("AUv3/aTamberPlugClassInfoLoadsThroughThePluginsReader") = []
{
    using TamberPlugFixtures::SVFPort;

    auto standIn = ScopedStandIn<SVFPort>(Module::gainStandIn);
    auto source = UnitHost<SVFPort>("Gain");
    auto* plug = plugFullState(source.unit.fullStateForDocument,
                               TamberPlugFixtures::svfClassInfo);

    auto target = UnitHost<SVFPort>("Gain");
    target.unit.fullStateForDocument = plug;

    auto& plugin = target.plugin();
    check(plugin.legacyLoads == 1);
    check(plugin.origin == ForeignOrigin::TamberPlugAU);
    check(plugin.context == StateContext::Session);
    check(TamberPlugFixtures::holdsSVFValues(plugin.params));
    check(near(target.parameterFor(plugin.params.highPass.cutoff).value, 0.4f));

    auto* saved = target.unit.fullStateForDocument;
    check([saved[@"MakeASoundState"] isKindOfClass:[NSData class]]);
    check(saved[@"TamberPlugState"] == nil);

    // Another component's codes never reach a plugin.
    auto echo = UnitHost<SynthPlugin>("Echo");
    echo.plugin().params.level.setValue(0.8f);
    echo.unit.fullStateForDocument = plug;
    check(near(echo.plugin().params.level.getValue(), 0.8f));
};

auto tJuceOffThread = test("AUv3/aJuceStateFromAWorkerLoadsOnTheMessageThread") = []
{
    auto standIn = ScopedStandIn<FixturePlugin>(Module::gainStandIn);
    auto source = UnitHost<FixturePlugin>("Gain");
    auto* juce = juceFullState(source.unit.fullStateForDocument);

    auto target = UnitHost<FixturePlugin>("Gain");
    auto* unit = target.unit;
    auto loader = std::thread([&] { unit.fullStateForDocument = juce; });
    loader.join();

    check(target.plugin().legacyLoads == 0);
    check(pumpUntil([&] { return target.plugin().legacyLoads == 1; }));
    check(JuceFixtures::holdsFixtureValues(target.plugin().params));
    check(near(target.parameterFor(target.plugin().params.cutoff).value, 440.f, 1e-2));
};

auto tNotes = test("AUv3/notesReachTheInstrumentAtTheirOffsets") = []
{
    auto host = UnitHost<SynthPlugin>("Echo");

    scheduleMidi(host, 10, {0x82, 64, 64});
    scheduleMidi(host, 5, {0x92, 64, 96});
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

    auto events = EventList {};
    auto start = static_cast<AUEventSampleTime>(host.sampleTime);
    events.midi(start + 7, {0xB1, 123, 0});
    events.midi(start - 20, {0x90, 60, 100});
    events.midi(AUEventSampleTimeImmediate + 3, {0xE0, 0, 96});
    check(host.renderEvents(maxBlock, events.head()) == noErr);

    check(plugin.numEvents == 3);
    check(plugin.events[0].isNoteOn() && plugin.events[0].sampleOffset == 0);
    check(plugin.events[1].isPitchBend() && plugin.events[1].sampleOffset == 3);
    check(near(plugin.events[1].asPitchBend()->value, 0.5));
    check(plugin.events[2].asControlChange()->controller == 123);
    check(plugin.events[2].sampleOffset == 7);
};

auto tSysEx = test("AUv3/sysExArrivesWhole") = []
{
    auto host = UnitHost<SynthPlugin>("Echo");

    scheduleMidi(host, 2, {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7});
    check(host.render(maxBlock) == noErr);

    check(host.plugin().numEvents == 1);
    const auto* sysEx = host.plugin().events[0].asSysEx();
    check(sysEx != nullptr && sysEx->size == 6);
    check(sysEx->data[1] == 0x7E && sysEx->data[5] == 0xF7);

    check(host.sent.size() == 1);
    check(host.sent[0].is({0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7}));
};

auto tMidiOut =
    test("AUv3/echoedEventsLeaveThroughTheMidiOutputBlockAtTheirOffsets") = []
{
    auto host = UnitHost<SynthPlugin>("Echo");

    scheduleMidi(host, 5, {0x92, 64, 96});
    scheduleMidi(host, 10, {0x82, 64, 64});
    scheduleMidi(host, 12, {0xB3, 123, 0});
    check(host.render(maxBlock) == noErr);

    check(host.sent.size() == 3);
    check(host.sent[0].bus == 0 && host.sent[0].offset == 5);
    check(host.sent[0].is({0x92, 64, 96}));
    check(host.sent[1].offset == 10 && host.sent[1].is({0x82, 64, 64}));
    check(host.sent[2].offset == 12 && host.sent[2].is({0xB3, 123, 0}));

    check(host.render(maxBlock) == noErr);
    check(host.sent.size() == 3);
};

auto tNoMidiBlock = test("AUv3/noMidiOutputBlockNoCrash") = []
{
    auto host = UnitHost<SynthPlugin>("Echo", Start::Open);
    host.collectMidi = false;
    check(host.allocate());

    scheduleMidi(host, 0, {0x90, 60, 100});
    check(host.render(maxBlock) == noErr);
    check(host.plugin().numEvents == 1);
    check(host.sent.empty());
};

auto tReset = test("AUv3/resetSilencesAHeldNote") = []
{
    auto holding = ScopedStandIn<HoldingSynth>(Module::echoStandIn);
    auto host = UnitHost<HoldingSynth>("Echo");

    scheduleMidi(host, 0, {0x90, 60, 100});
    check(host.render(maxBlock) == noErr);
    check(host.outputs[0].storage[0][maxBlock - 1] == 1.f);

    check(host.render(maxBlock) == noErr);
    check(host.outputs[0].storage[0][0] == 1.f);

    auto resets = host.plugin().resets;
    [host.unit reset];
    check(host.render(maxBlock) == noErr);

    check(host.plugin().resets == resets + 1);
    check(host.outputs[0].storage[0][0] == 0.f);
    check(host.outputs[0].storage[1][maxBlock - 1] == 0.f);
};

auto tResetFirst = test("AUv3/aResetBeforeAnyRenderIsHarmless") = []
{
    auto holding = ScopedStandIn<HoldingSynth>(Module::echoStandIn);
    auto host = UnitHost<HoldingSynth>("Echo");

    [host.unit reset];
    host.outputs[0].fill(-1.f);
    check(host.render(maxBlock) == noErr);
    check(host.outputs[0].storage[0][0] == 0.f);

    scheduleMidi(host, 0, {0x90, 60, 100});
    check(host.render(maxBlock) == noErr);
    check(host.outputs[0].storage[0][0] == 1.f);
};

auto tLatency = test("AUv3/aLatencyChangeIsObservedOnceTheHostHasABaseline") = []
{
    auto latency = ScopedStandIn<LatencyPlugin>(Module::gainStandIn);
    auto host = UnitHost<LatencyPlugin>("Gain");
    auto& plugin = host.plugin();
    auto observed = ScopedKeyObserver(host.unit, @"latency");

    plugin.params.latency.setValue(480.f);
    plugin.announceLatency();
    check(observed.count() == 0);

    check(near(host.unit.latency, 480.0 / sampleRate));

    plugin.params.latency.setValue(240.f);
    plugin.announceLatency();
    check(observed.count() == 1);

    plugin.announceLatency();
    check(observed.count() == 1);

    auto& edits = *plugin.hostEditListener();
    edits.beginParameterEdit(0);
    plugin.params.latency.setValue(96.f);
    edits.performParameterEdit(0, plugin.params.latency.getNormalized());
    edits.endParameterEdit(0);
    check(observed.count() == 2);

    host.parameterFor(plugin.params.latency).value = 128.f;
    check(observed.count() == 3);
    check(near(host.unit.latency, 128.0 / sampleRate));

    auto other = UnitHost<LatencyPlugin>("Gain");
    other.plugin().params.latency.setValue(700.f);
    host.unit.fullStateForDocument = other.unit.fullStateForDocument;
    check(observed.count() == 4);
    check(near(host.unit.latency, 700.0 / sampleRate));

    plugin.tail = 4800;
    check(near(host.unit.tailTime, 0.1));
};

auto tTreeRebuilt = test("AUv3/parameterInfoChangedRebuildsTheTree") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto observed = ScopedKeyObserver(host.unit, @"parameterTree");
    auto* before = host.unit.parameterTree;
    auto* oldGain = host.parameterFor(host.plugin().params.gain);

    host.plugin().notifyHostParameterInfoChanged();
    check(observed.count() == 1);

    auto* after = host.unit.parameterTree;
    check(after != before);
    check(after.allParameters.count == before.allParameters.count);

    // The old tree is cut loose: a write there reaches nothing.
    oldGain.value = -30.f;
    check(near(host.plugin().params.gain.getValue(), 0.f));

    host.parameterFor(host.plugin().params.gain).value = -30.f;
    check(near(host.plugin().params.gain.getValue(), -30.f));
};

auto tBypassAnswered = test("AUv3/bypassFollowsTheBypassParameter") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& bypass = host.plugin().params.bypass;

    check(!host.unit.shouldBypassEffect);
    bypass.setValue(1.f);
    check(host.unit.shouldBypassEffect);
    bypass.setValue(0.f);
    check(!host.unit.shouldBypassEffect);

    auto echo = UnitHost<SynthPlugin>("Echo");
    echo.unit.shouldBypassEffect = YES;
    check(!echo.unit.shouldBypassEffect);
};

auto tBypassWrites = test("AUv3/writingBypassMovesTheParameterAndTheTree") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& params = host.plugin().params;
    auto* node = host.parameterFor(params.bypass);
    auto observed = ScopedKeyObserver(host.unit, @"shouldBypassEffect");

    host.unit.shouldBypassEffect = YES;
    check(params.bypass.isOn());
    check(node.value == 1.f);
    check(observed.count() == 1);

    check(host.render(maxBlock) == noErr);
    check(params.bypass.isOn());

    host.unit.shouldBypassEffect = NO;
    check(!params.bypass.isOn());
    check(node.value == 0.f);

    // A tree write that flips it, and an editor edit, are observed too.
    node.value = 1.f;
    check(observed.count() == 3);
    node.value = 1.f;
    host.parameterFor(params.gain).value = -6.f;
    check(observed.count() == 3);

    auto& edits = *host.plugin().hostEditListener();
    auto index = host.plugin().parameters().indexOf(&params.bypass);
    edits.beginParameterEdit(index);
    params.bypass.setValue(0.f);
    edits.performParameterEdit(index, params.bypass.getNormalized());
    edits.endParameterEdit(index);
    check(observed.count() == 4);
    check(!host.unit.shouldBypassEffect);
};

auto tBypassHeld = test("AUv3/aHeldBypassDropsTheWriteAndMirrorsItBack") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto& bypass = host.plugin().params.bypass;
    auto* node = host.parameterFor(bypass);
    auto& edits = *host.plugin().hostEditListener();
    auto index = host.plugin().parameters().indexOf(&bypass);

    edits.beginParameterEdit(index);
    host.unit.shouldBypassEffect = YES;
    check(!bypass.isOn());
    check(!host.unit.shouldBypassEffect);
    check(node.value == 0.f);

    edits.endParameterEdit(index);

    host.unit.shouldBypassEffect = YES;
    check(bypass.isOn());
};

// An effect with no bypass parameter answers shouldBypassEffect through the one
// the wrapper adds, and the host's bypass reaches the audio.
auto tAddedBypass = test("AUv3/theWrappersBypassAnswersShouldBypassEffect") = []
{
    auto standIn = ScopedStandIn<UnbypassedGainPlugin>(Module::gainStandIn);
    auto host = UnitHost<UnbypassedGainPlugin>("Gain");
    const auto& list = host.plugin().parameters();
    auto index = list.bypassIndex();

    check(index >= 0 && list.entry(index).id == "__bypass");
    check(!host.unit.shouldBypassEffect);

    host.plugin().params.gain.setValue(6.f);
    host.inputs[0].fill(0.5f);

    host.unit.shouldBypassEffect = YES;
    check(host.unit.shouldBypassEffect);
    check(list[index].getValue() == 1.f);

    // Past the 480-sample crossfade.
    for (auto b = 0; b < 10; ++b)
        host.render(blockSize);

    check(host.renderedSample(0, 0) == 0.5f);
    check(host.renderedSample(1, blockSize - 1) == 0.5f);

    host.unit.shouldBypassEffect = NO;
    check(list[index].getValue() == 0.f);
};

// A JUCE port under its JUCE build's ids (JucePort.h), a unit over its own
// description.

using JucePort::PortPlugin;

const PluginDescription& juceDescription()
{
    return JucePort::description(IdScheme::JUCE);
}

auto tPortTree = test("AUv3/aJucePortsTreeIsJuces") = []
{
    auto host = UnitHost<PortPlugin>(juceDescription());
    auto& p = host.plugin().params;
    auto* tree = host.unit.parameterTree;

    check(host.unit.componentDescription.componentType
          == kAudioUnitType_MusicEffect);

    // Every parameter but the added bypass, the non-automatable one included,
    // each at JUCE's 64-bit address under its bare key.
    check(tree.allParameters.count == 6);
    check([tree parameterWithAddress:juceVst3BypassId] == nil);

    for (const auto& ids: JucePort::juceIds)
    {
        auto* node = [tree parameterWithAddress:ids.address];
        check(node != nil);
        check(toUtf8(node.identifier) == ids.id);
    }

    auto* cutoff = host.parameterFor(p.cutoff);
    check(cutoff.address == 1052795674147ull);
    check(cutoff.minValue == 0.f && cutoff.maxValue == 1.f);
    check(cutoff.unit == kAudioUnitParameterUnit_Generic);
    check((cutoff.flags & kAudioUnitParameterFlag_CanRamp) != 0);
    check((cutoff.flags & kAudioUnitParameterFlag_IsHighResolution) != 0);
    check((cutoff.flags & kAudioUnitParameterFlag_NonRealTime) == 0);
    check(cutoff.valueStrings == nil);
    check(near(cutoff.value, 0.404752135, 1e-6));

    auto* voices = host.parameterFor(p.voices);
    check(voices.maxValue == 1.f && voices.unit == kAudioUnitParameterUnit_Generic);
    check(near(voices.value, 0.466666669, 1e-6));

    auto* mode = host.parameterFor(p.mode);
    check(mode.maxValue == 2.f && mode.unit == kAudioUnitParameterUnit_Indexed);
    check([mode.valueStrings isEqualToArray:@[ @"Clean", @"Warm", @"Hot" ]]);
    check(mode.value == 1.f);

    auto* enabled = host.parameterFor(p.enabled);
    check(enabled.maxValue == 1.f
          && enabled.unit == kAudioUnitParameterUnit_Boolean);
    check(enabled.valueStrings.count == 2);

    auto* quality = host.parameterFor(p.quality);
    check((quality.flags & kAudioUnitParameterFlag_NonRealTime) != 0);

    // A group is named by its own id, as JUCE's tree names it.
    auto* env = static_cast<AUParameterGroup*>(tree.children[4]);
    check([env isKindOfClass:[AUParameterGroup class]]);
    check([env.identifier isEqualToString:@"env"]);
    check([env.children[0].identifier isEqualToString:@"attack"]);
    check(static_cast<AUParameter*>(env.children[0]).address == 1031671269632ull);
};

auto tPortSeed = test("AUv3/aJuceTreeIsSeededWithTheNormalizedDefault") = []
{
    // JUCE sets each AUParameter's first value to the normalized default, not
    // the current value in the host's space; seen with no value provider.
    auto params = JucePort::PortParams {};
    params.cutoff.setValue(5000.f);
    params.enabled.setValue(0.f);

    auto list = ParameterList {params, policyFor(IdScheme::JUCE, {}).parameters};
    auto* tree = AUv3::makeParameterTree(
        list, AU::ParameterSpace(IdScheme::JUCE, JuceSettings {}));
    auto* cutoff = [tree parameterWithAddress:1052795674147ull];
    auto* enabled = [tree parameterWithAddress:108379842206215ull];

    check(near(cutoff.value, 0.404752135, 1e-6));
    check(enabled.value == 1.f);

    // MakeASound's tree starts at the current value.
    auto native = ParameterList {params};
    auto* nativeTree = AUv3::makeParameterTree(native);
    auto* nativeEnabled =
        [nativeTree parameterWithAddress:native.entry(2).address];
    check(nativeEnabled.value == 0.f);
};

auto tPortEvents = test("AUv3/aJucePortsEventsAndTreeFindThe64BitAddress") = []
{
    auto host = UnitHost<PortPlugin>(juceDescription());
    auto& p = host.plugin().params;
    auto now = static_cast<AUEventSampleTime>(host.sampleTime);

    auto events = EventList {};
    events.parameter(1052795674147ull, 0.25f, now);
    events.parameter(113445321ull, 2.f, now);
    events.parameter(1031671269632ull, p.env.attack.toNormalized(0.5f), now);
    check(host.renderEvents(maxBlock, events.head()) == noErr);

    check(near(p.cutoff.getValue(), p.cutoff.toPlain(0.25f), 1e-2));
    check(p.mode.getIndex() == 2);
    check(near(p.env.attack.getValue(), 0.5f, 1e-4));

    // An address no parameter has, 32 bits of a real one included, moves nothing.
    events.clear();
    events.parameter(1052795674147ull & 0xffffffffu, 0.9f, now);
    check(host.renderEvents(maxBlock, events.head()) == noErr);
    check(near(p.cutoff.getValue(), p.cutoff.toPlain(0.25f), 1e-2));

    // A tree write, a plugin move and the text, in JUCE's space.
    host.parameterFor(p.voices).value = 1.f;
    check(near(p.voices.getValue(), 16.f));

    p.enabled.setValue(0.f);
    check(pumpUntil([&] { return host.parameterFor(p.enabled).value == 0.f; }));

    auto two = AUValue {2.f};
    check(toUtf8([host.parameterFor(p.mode) stringFromValue:&two]) == "Hot");
    check([host.parameterFor(p.mode) valueFromString:@"Warm"] == 1.f);
};

auto tPortBypass = test("AUv3/aJucePortsAddedBypassIsShouldBypassEffectAlone") = []
{
    auto host = UnitHost<PortPlugin>(juceDescription());
    auto& plugin = host.plugin();

    check(!host.unit.shouldBypassEffect);
    host.unit.shouldBypassEffect = YES;
    check(host.unit.shouldBypassEffect);
    check(JuceFixtures::addedBypassOf(plugin) == 1.f);
    check([host.unit.parameterTree parameterWithAddress:juceVst3BypassId] == nil);

    host.unit.shouldBypassEffect = NO;
    check(JuceFixtures::addedBypassOf(plugin) == 0.f);
};

auto tPortLegacy = test("AUv3/aJucePortWithLegacyIdsIsAddressedByIndex") = []
{
    auto host =
        UnitHost<PortPlugin>(JucePort::description(IdScheme::JUCELegacyIndex));
    auto& p = host.plugin().params;
    auto* tree = host.unit.parameterTree;

    check(tree.allParameters.count == 6);

    for (NSUInteger i = 0; i < tree.allParameters.count; ++i)
        check(tree.allParameters[i].address == i);

    check([host.parameterFor(p.cutoff).identifier isEqualToString:@"cutoff"]);

    // The AUv3 keeps its value strings under legacy ids, as JUCE's does.
    check(host.parameterFor(p.mode).valueStrings.count == 3);

    auto events = EventList {};
    events.parameter(1, 2.f, static_cast<AUEventSampleTime>(host.sampleTime));
    check(host.renderEvents(maxBlock, events.head()) == noErr);
    check(p.mode.getIndex() == 2);
};

auto tPortJuceState = test("AUv3/aJuceStateRestoresIntoAJucePort") = []
{
    auto source = UnitHost<PortPlugin>(juceDescription());
    auto* juce = juceFullState(source.unit.fullStateForDocument);

    auto target = UnitHost<PortPlugin>(juceDescription());
    target.unit.fullStateForDocument = juce;

    auto& p = target.plugin().params;
    check(target.plugin().legacyLoads == 1);
    check(near(p.cutoff.getValue(), 440.f, 1e-2));
    check(p.mode.getIndex() == 2);
    check(!p.enabled.isOn());
    check(near(p.voices.getValue(), 5.f));
    check(near(p.env.attack.getValue(), 0.25f));

    // The tree, in JUCE's space.
    check(near(target.parameterFor(p.cutoff).value,
               p.cutoff.toNormalized(440.f),
               1e-5));
    check(target.parameterFor(p.mode).value == 2.f);
};

// A Tamber Plug port (IdScheme::TamberPlug): the tree has every exposed
// parameter at its index, under its id path, but not the added bypass, which
// shouldBypassEffect drives.
auto tPlugTree = test("AUv3/aTamberPlugPortsTreeLeavesTheBypassOut") = []
{
    using TamberPlugFixtures::SVFPort;

    auto host = UnitHost<SVFPort>(*TamberPlugFixtures::plugDescriptionFor(
        FourCC("Psvf").toUint32()));
    auto* tree = host.unit.parameterTree;
    const auto& list = host.plugin().parameters();

    check(tree.allParameters.count
          == static_cast<NSUInteger>(TamberPlugFixtures::svfParameterCount));
    check([tree parameterWithAddress:4].identifier != nil);
    check([[tree parameterWithAddress:4].identifier isEqualToString:@"BP/Cutoff"]);
    check([tree parameterWithAddress:list.entry(list.bypassIndex()).hostId] == nil);

    host.unit.shouldBypassEffect = YES;
    check(host.unit.shouldBypassEffect);
    check(list[list.bypassIndex()].getValue() == 1.f);
};
} // namespace
