// PluginWrapper driven the way a format adapter drives it, with no host: layout
// negotiation, parameters by index and by host id, state through the wrapper's
// threading rules, how the buses reach the plugin in place and not, and MIDI in
// and out of a block.

#include "TestPlugins.h"

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

using namespace nano;
using namespace TestPlugins;

namespace MakeASound
{
ModuleDescription describeModule()
{
    auto module = ModuleDescription {};
    module.vendor = "MakeASound Tests";
    module.manufacturerCode = "MaSo";
    module.plugins.add(
        {.name = "Gain",
         .pluginCode = "Gain",
         .create = [] { return EA::makeOwned<TestPlugins::GainPlugin>(); }});
    return module;
}
} // namespace MakeASound

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
std::atomic<int> deferredLoads {0};

struct CountingGainPlugin : GainPlugin
{
    void loadStateExceptParameters(std::string_view data,
                                   StateContext context) override
    {
        GainPlugin::loadStateExceptParameters(data, context);
        ++deferredLoads;
        ++deferredHere;
    }

    int deferredHere = 0;
};

// A plugin with no snapshot, whose message-thread save fails.
struct ThrowingPlugin : Plugin
{
    std::string_view name() const override { return "Throwing"; }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    std::string saveState(StateContext) override
    { throw std::runtime_error("no document"); }
};

struct RecordingListener : HostEditListener
{
    void beginParameterEdit(int) noexcept override {}
    void performParameterEdit(int, float) noexcept override {}
    void endParameterEdit(int) noexcept override {}
    void latencyChanged() noexcept override { ++latency; }
    void parameterInfoChanged() noexcept override { ++info; }

    int latency = 0;
    int info = 0;
};

bool near(float a, float b)
{ return std::abs(a - b) < 1e-4f; }

template <typename T = GainPlugin>
PluginWrapper makeWrapper(PluginFormat format = PluginFormat::Unknown)
{ return PluginWrapper(EA::makeOwned<T>(), format); }

template <typename T>
T& pluginOf(PluginWrapper& wrapper)
{ return static_cast<T&>(wrapper.plugin()); }

Block ramp(float first)
{
    auto block = Block {};

    for (auto i = 0; i < blockSize; ++i)
        block[i] = first + static_cast<float>(i);

    return block;
}

bool equals(const Block& block, const Block& expected)
{ return block == expected; }

bool isSilent(const Block& block)
{
    for (auto sample: block)
        if (sample != 0.f)
            return false;

    return true;
}

template <typename Predicate>
bool pumpUntil(Predicate ready)
{ return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {5000}); }

bool contains(const std::string& text, const std::string& part)
{ return text.find(part) != std::string::npos; }

std::string sessionDocument(float gain, const std::string& preset)
{
    auto source = makeWrapper();
    source.setParameter(0, gain);
    pluginOf<GainPlugin>(source).state.preset = preset;
    pluginOf<GainPlugin>(source).state.markChanged();
    return source.saveState(StateContext::Session);
}

void loadOffMessageThread(PluginWrapper& wrapper, const std::string& document)
{
    auto host =
        std::thread([&] { wrapper.loadState(document, StateContext::Session); });
    host.join();
}

auto tDescribeModule = test("Plugin/theModuleCreatesItsPlugins") = []
{
    auto module = describeModule();
    check(module.plugins.size() == 1);

    auto wrapper = PluginWrapper(module.plugins[0].create());
    check(wrapper.plugin().name() == "Gain");
};

auto tAcceptsDeclared = test("Plugin/theDefaultAcceptsOnlyTheDeclaredLayout") = []
{
    auto wrapper = makeWrapper<SynthPlugin>();
    auto& plugin = wrapper.plugin();
    auto declared = plugin.getBusLayout();

    check(plugin.acceptsLayout(declared));

    auto mono = declared;
    mono.outputs[0].numChannels = 1;
    check(!plugin.acceptsLayout(mono));
    check(!wrapper.setLayout(mono));
    check(wrapper.busLayout() == declared);
};

auto tSetLayout = test("Plugin/setLayoutResizesTheContext") = []
{
    auto wrapper = makeWrapper();
    auto& plugin = pluginOf<GainPlugin>(wrapper);

    auto wide = BusLayout::stereoInOut();
    wide.outputs[0].numChannels = 1;
    check(!wrapper.setLayout(wide));

    auto withSidechain = BusLayout::stereoInOut();
    withSidechain.inputs.add({"Sidechain", 1});
    check(wrapper.setLayout(withSidechain));
    check(wrapper.busLayout() == withSidechain);

    wrapper.prepare(48000, blockSize);
    check(plugin.spec.layout == withSidechain);
    check(plugin.spec.sampleRate == 48000);
    check(plugin.spec.maxBlockSize == blockSize);

    wrapper.process();
    check(plugin.inputBuses == 2);
};

auto tParameterIds = test("Plugin/parametersCarryIdsAndHashedHostIds") = []
{
    auto wrapper = makeWrapper();
    auto& list = wrapper.plugin().parameters();

    check(list.size() == 5);
    check(list.entry(0).id == "Gain");
    check(list.entry(1).id == "Filter/Cutoff");
    check(list.entry(4).id == "Width");
    check(list.entry(4).sessionOnly);

    for (auto i = 0; i < list.size(); ++i)
    {
        check(list.entry(i).hostId == hostIdFor(list.entry(i).id));
        check(list.indexOfHostId(list.entry(i).hostId) == i);
    }
};

auto tByHostId = test("Plugin/setParameterByHostIdLandsAndIgnoresUnknownIds") = []
{
    auto wrapper = makeWrapper();
    auto& params = pluginOf<GainPlugin>(wrapper).params;

    wrapper.setParameterByHostId(hostIdFor("Mode"), 1.f);
    check(params.mode.getIndex() == 2);

    wrapper.setParameterByHostId(hostIdFor("Filter/Cutoff"), 0.f);
    check(near(params.filter.cutoff.get(), 20.f));

    auto& list = wrapper.plugin().parameters();
    auto before = Vector<float>();

    for (auto i = 0; i < list.size(); ++i)
        before.add(wrapper.getParameter(i));

    wrapper.setParameterByHostId(hostIdFor("nothing"), 1.f);

    for (auto i = 0; i < list.size(); ++i)
        check(wrapper.getParameter(i) == before[i]);
};

auto tByIndex = test("Plugin/parametersRoundTripByIndex") = []
{
    auto wrapper = makeWrapper();

    wrapper.setNormalizedParameter(0, 1.f);
    check(near(wrapper.getParameter(0), 12.f));
    check(near(wrapper.getNormalizedParameter(0), 1.f));

    wrapper.setParameter(0, -60.f);
    check(near(wrapper.getNormalizedParameter(0), 0.f));

    wrapper.setParameter(3, 1.f);
    check(pluginOf<GainPlugin>(wrapper).params.bypass.isOn());

    wrapper.setNormalizedParameter(-1, 1.f);
    wrapper.setNormalizedParameter(99, 1.f);
    wrapper.setParameter(99, 1.f);
    check(wrapper.getParameter(99) == 0.f);
    check(wrapper.getNormalizedParameter(-1) == 0.f);
    check(near(wrapper.getParameter(0), -60.f));
};

auto tHolds = test("Plugin/parameterHoldsNest") = []
{
    auto wrapper = makeWrapper();

    wrapper.holdParameter(1);
    wrapper.holdParameter(1);
    wrapper.releaseParameter(1);
    check(wrapper.isParameterHeld(1));

    wrapper.releaseParameter(1);
    check(!wrapper.isParameterHeld(1));

    wrapper.releaseParameter(1);
    wrapper.holdParameter(1);
    check(wrapper.isParameterHeld(1));
    check(!wrapper.isParameterHeld(0));

    wrapper.holdParameter(99);
    wrapper.releaseParameter(-1);
    check(!wrapper.isParameterHeld(99));
};

auto tHeldDropsHostWrites = test("Plugin/aHeldParameterDropsHostWrites") = []
{
    auto wrapper = makeWrapper();
    auto& params = pluginOf<GainPlugin>(wrapper).params;
    auto hostId = wrapper.plugin().parameters().entry(0).hostId;

    wrapper.holdParameter(0);
    wrapper.setNormalizedParameter(0, 1.f);
    wrapper.setParameter(0, -60.f);
    wrapper.setParameterByHostId(hostId, 1.f);
    check(near(params.gain.get(), 0.f));

    params.gain.setValue(-6.f);
    check(near(params.gain.get(), -6.f));

    wrapper.setParameter(1, 500.f);
    check(near(params.filter.cutoff.get(), 500.f));

    wrapper.releaseParameter(0);
    wrapper.setParameterByHostId(hostId, 1.f);
    check(near(params.gain.get(), 12.f));
};

auto tStateRoundTrip = test("Plugin/stateRoundTripsThroughTheWrapper") = []
{
    auto saved = makeWrapper();
    auto& source = pluginOf<GainPlugin>(saved);
    saved.setParameter(0, -6.f);
    saved.setParameter(2, 2.f);
    saved.setParameter(4, 0.9f);
    source.state.preset = "Loud";
    source.state.markChanged();

    auto session = saved.saveState(StateContext::Session);
    auto preset = saved.saveState(StateContext::Preset);

    auto loaded = makeWrapper();
    auto& target = pluginOf<GainPlugin>(loaded);
    loaded.loadState(session, StateContext::Session);

    check(near(target.params.gain.get(), -6.f));
    check(target.params.mode.getIndex() == 2);
    check(near(target.params.width.get(), 0.9f));
    check(target.state.preset == "Loud");

    auto fromPreset = makeWrapper();
    fromPreset.loadState(preset, StateContext::Preset);
    check(near(pluginOf<GainPlugin>(fromPreset).params.width.get(), 0.5f));
    check(pluginOf<GainPlugin>(fromPreset).state.preset == "Loud");
};

auto tInsertedParameter =
    test("Plugin/aParameterInsertedMidListKeepsOlderIdsAndValues") = []
{
    auto older = makeWrapper();
    older.setParameter(0, -12.f);
    older.setParameter(1, 5000.f);
    older.setParameter(2, 1.f);
    older.setParameter(3, 1.f);

    auto newer = makeWrapper<NewerGainPlugin>();
    newer.loadState(older.saveState(StateContext::Session), StateContext::Session);

    auto& oldList = older.plugin().parameters();
    auto& newList = newer.plugin().parameters();
    check(newList.size() == oldList.size() + 1);

    for (const auto& entry: oldList)
    {
        auto index = newList.indexOfHostId(entry.hostId);
        check(index >= 0);
        check(newList.entry(index).id == entry.id);
        check(newer.getParameter(index) == entry.param->getValue());
    }

    check(near(pluginOf<NewerGainPlugin>(newer).params.drive.get(), 0.25f));
};

auto tDeferredLoad = test("Plugin/loadStateAppliesParametersNowAndTheRestLater") = []
{
    auto source = makeWrapper();
    source.setParameter(0, 6.f);
    pluginOf<GainPlugin>(source).state.preset = "Later";
    pluginOf<GainPlugin>(source).state.markChanged();
    auto document = source.saveState(StateContext::Session);

    auto wrapper = makeWrapper();
    auto& plugin = pluginOf<GainPlugin>(wrapper);

    auto host =
        std::thread([&] { wrapper.loadState(document, StateContext::Session); });
    host.join();

    check(near(plugin.params.gain.get(), 6.f));
    check(plugin.state.preset == "Init");

    check(pumpUntil([&] { return plugin.state.preset == "Later"; }));
};

auto tLoadPastTeardown = test("Plugin/aLoadQueuedPastTeardownDrops") = []
{
    auto document = makeWrapper().saveState(StateContext::Session);

    {
        auto wrapper = makeWrapper<CountingGainPlugin>();
        auto host =
            std::thread([&] { wrapper.loadState(document, StateContext::Session); });
        host.join();
    }

    eacp::Threads::runEventLoopFor(eacp::Time::MS {100});
    check(deferredLoads == 0);

    auto alive = makeWrapper<CountingGainPlugin>();
    auto host =
        std::thread([&] { alive.loadState(document, StateContext::Session); });
    host.join();

    check(pumpUntil([] { return deferredLoads == 1; }));
};

auto tWriteBeforeDrain =
    test("Plugin/aParameterWrittenBeforeTheDeferredHalfSurvivesIt") = []
{
    auto document = sessionDocument(6.f, "Later");
    auto wrapper = makeWrapper();
    auto& plugin = pluginOf<GainPlugin>(wrapper);

    loadOffMessageThread(wrapper, document);
    check(near(plugin.params.gain.get(), 6.f));

    wrapper.setParameter(0, -3.f);
    check(pumpUntil([&] { return plugin.state.preset == "Later"; }));
    check(near(plugin.params.gain.get(), -3.f));
};

auto tStaleQueuedLoad = test("Plugin/onlyTheLatestQueuedLoadRuns") = []
{
    auto first = sessionDocument(-6.f, "A");
    auto second = sessionDocument(3.f, "B");
    auto wrapper = makeWrapper<CountingGainPlugin>();
    auto& plugin = pluginOf<CountingGainPlugin>(wrapper);

    loadOffMessageThread(wrapper, first);
    loadOffMessageThread(wrapper, second);

    check(pumpUntil([&] { return plugin.state.preset == "B"; }));
    eacp::Threads::runEventLoopFor(eacp::Time::MS {50});

    check(plugin.deferredHere == 1);
    check(plugin.state.preset == "B");
    check(near(plugin.params.gain.get(), 3.f));
};

auto tQueuedBeforeMessageThreadLoad =
    test("Plugin/aLoadQueuedBeforeAMessageThreadLoadDrops") = []
{
    auto queued = sessionDocument(-6.f, "Queued");
    auto direct = sessionDocument(3.f, "Direct");
    auto wrapper = makeWrapper<CountingGainPlugin>();
    auto& plugin = pluginOf<CountingGainPlugin>(wrapper);

    loadOffMessageThread(wrapper, queued);
    wrapper.loadState(direct, StateContext::Session);
    check(plugin.state.preset == "Direct");

    eacp::Threads::runEventLoopFor(eacp::Time::MS {100});

    check(plugin.state.preset == "Direct");
    check(near(plugin.params.gain.get(), 3.f));
};

auto tSaveOffMessageThread =
    test("Plugin/saveStateOffTheMessageThreadNeverWaits") = []
{
    auto wrapper = makeWrapper();
    auto& plugin = pluginOf<GainPlugin>(wrapper);
    plugin.state.preset = "Snapshot";
    plugin.state.markChanged();
    wrapper.setParameter(0, 3.f);

    // Nothing pumps the message thread here, so a save that marshalled would hang.
    auto saving =
        std::async(std::launch::async,
                   [&] { return wrapper.saveState(StateContext::Session); });

    check(saving.wait_for(std::chrono::seconds(5)) == std::future_status::ready);

    auto loaded = makeWrapper();
    loaded.loadState(saving.get(), StateContext::Session);
    check(near(pluginOf<GainPlugin>(loaded).params.gain.get(), 3.f));
    check(pluginOf<GainPlugin>(loaded).state.preset == "Snapshot");
};

auto tPresetOffMessageThread =
    test("Plugin/aPresetSaveOffTheMessageThreadNeverWaits") = []
{
    auto wrapper = makeWrapper();
    auto& plugin = pluginOf<GainPlugin>(wrapper);
    plugin.state.preset = "Shared";
    plugin.state.markChanged();
    wrapper.setParameter(4, 0.9f);

    auto saving =
        std::async(std::launch::async,
                   [&]
                   {
                       return std::pair {wrapper.saveState(StateContext::Preset),
                                         wrapper.saveState(StateContext::Session)};
                   });

    check(saving.wait_for(std::chrono::seconds(5)) == std::future_status::ready);

    auto [preset, session] = saving.get();
    check(!contains(preset, "Width"));
    check(contains(preset, R"("preset":"Shared")"));
    check(contains(session, R"("Width":0.9)"));
};

auto tThrowingSave = test("Plugin/aThrowingMarshalledSaveReachesTheCaller") = []
{
    auto wrapper = makeWrapper<ThrowingPlugin>();

    auto saving = std::async(std::launch::async,
                             [&]
                             {
                                 try
                                 {
                                     wrapper.saveState(StateContext::Session);
                                 }
                                 catch (const std::runtime_error&)
                                 {
                                     return true;
                                 }

                                 return false;
                             });

    check(pumpUntil(
        [&]
        {
            return saving.wait_for(std::chrono::seconds(0))
                   == std::future_status::ready;
        }));
    check(saving.get());
};

auto tDetached = test("Plugin/busesAreDetachedAfterEveryBlock") = []
{
    auto wrapper = makeWrapper();
    auto& plugin = pluginOf<GainPlugin>(wrapper);
    wrapper.prepare(48000, blockSize);

    auto left = ramp(1.f);
    auto right = ramp(100.f);
    float* channels[] = {left.data(), right.data()};

    wrapper.bindInput(0, channels, 2, blockSize);
    wrapper.bindOutput(0, channels, 2, blockSize, channels, 2);
    wrapper.process();
    check(plugin.boundSamples == 4 * blockSize);

    left = ramp(1.f);
    right = ramp(100.f);
    wrapper.setParameter(0, 6.f);
    wrapper.process();

    check(plugin.boundSamples == 0);
    check(equals(left, ramp(1.f)));
    check(equals(right, ramp(100.f)));
};

auto tInPlace = test("Plugin/inPlaceBuffersAreNeitherCopiedNorZeroed") = []
{
    auto wrapper = makeWrapper();
    auto& plugin = pluginOf<GainPlugin>(wrapper);
    wrapper.prepare(48000, blockSize);

    auto left = ramp(1.f);
    auto right = ramp(100.f);
    float* channels[] = {left.data(), right.data()};

    wrapper.bindInput(0, channels, 2, blockSize);
    wrapper.bindOutput(0, channels, 2, blockSize, channels, 2);
    wrapper.setParameter(0, 20.f * std::log10(2.f));
    wrapper.process();

    check(equals(plugin.seenInput[0], ramp(1.f)));
    check(equals(plugin.seenInput[1], ramp(100.f)));
    check(equals(plugin.seenOutput[0], ramp(1.f)));
    check(equals(plugin.seenOutput[1], ramp(100.f)));
    check(near(left[3], 8.f));
    check(near(right[0], 200.f));
};

auto tSeparate = test("Plugin/separateBuffersCopyTheInputAcross") = []
{
    auto wrapper = makeWrapper();
    auto& plugin = pluginOf<GainPlugin>(wrapper);
    wrapper.prepare(48000, blockSize);

    auto inLeft = ramp(1.f);
    auto inRight = ramp(100.f);
    auto outLeft = ramp(-50.f);
    auto outRight = ramp(-50.f);
    const float* inputs[] = {inLeft.data(), inRight.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    wrapper.bindInput(0, inputs, 2, blockSize);
    wrapper.bindOutput(0, outputs, 2, blockSize, inputs, 2);
    wrapper.process();

    check(equals(plugin.seenOutput[0], ramp(1.f)));
    check(equals(plugin.seenOutput[1], ramp(100.f)));
    check(equals(inLeft, ramp(1.f)));
};

auto tMonoIntoStereo = test("Plugin/aMonoInputFillsChannelZeroAndZeroesTheRest") = []
{
    auto wrapper = makeWrapper();
    auto& plugin = pluginOf<GainPlugin>(wrapper);

    auto input = ramp(1.f);
    auto outLeft = ramp(-50.f);
    auto outRight = ramp(-50.f);
    const float* inputs[] = {input.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    wrapper.bindInput(0, inputs, 1, blockSize);
    wrapper.bindOutput(0, outputs, 2, blockSize, inputs, 1);
    wrapper.process();

    check(equals(plugin.seenOutput[0], ramp(1.f)));
    check(isSilent(plugin.seenOutput[1]));
};

auto tAliasedMono = test("Plugin/anAliasedMonoInputSurvivesAWiderOutput") = []
{
    auto wrapper = makeWrapper();
    auto& plugin = pluginOf<GainPlugin>(wrapper);

    auto left = ramp(1.f);
    auto right = ramp(100.f);
    const float* inputs[] = {left.data()};
    float* outputs[] = {left.data(), right.data()};

    wrapper.bindInput(0, inputs, 1, blockSize);
    wrapper.bindOutput(0, outputs, 2, blockSize, inputs, 1);
    wrapper.process();

    check(equals(plugin.seenInput[0], ramp(1.f)));
    check(equals(plugin.seenOutput[0], ramp(1.f)));
    check(isSilent(plugin.seenOutput[1]));
};

auto tNoMatchingInput = test("Plugin/anOutputWithNoInputIsZeroed") = []
{
    auto wrapper = makeWrapper<SynthPlugin>();
    auto& plugin = pluginOf<SynthPlugin>(wrapper);
    plugin.params.level.setValue(0.f);

    auto left = ramp(1.f);
    auto right = ramp(100.f);
    float* outputs[] = {left.data(), right.data()};

    wrapper.bindOutput(0, outputs, 2, blockSize);
    wrapper.process();

    check(isSilent(left));
    check(isSilent(right));
};

auto tMidiSorted = test("Plugin/midiInArrivesSortedAndStable") = []
{
    auto wrapper = makeWrapper<SynthPlugin>();
    auto& plugin = pluginOf<SynthPlugin>(wrapper);

    wrapper.clearMidi();
    check(wrapper.pushMidiIn(0, MIDI::Event::noteOn(0, 60, 1.f, 10)));
    check(wrapper.pushMidiIn(0, MIDI::Event::noteOff(0, 62, 0.f, 3)));
    check(wrapper.pushMidiIn(0, MIDI::Event::noteOn(0, 62, 1.f, 3)));
    check(wrapper.pushMidiIn(0, MIDI::Event::controlChange(0, 1, 0.5f, 0)));
    check(!wrapper.pushMidiIn(1, MIDI::Event::noteOn(0, 60, 1.f)));
    check(!wrapper.pushMidiIn(-1, MIDI::Event::noteOn(0, 60, 1.f)));

    wrapper.sortMidiInByOffset();
    wrapper.process();

    check(plugin.numEvents == 4);
    check(plugin.events[0].isControlChange());
    check(plugin.events[1].isNoteOff());
    check(plugin.events[2].isNoteOn() && plugin.events[2].sampleOffset == 3);
    check(plugin.events[3].isNoteOn() && plugin.events[3].sampleOffset == 10);
};

auto tMidiCapacity = test("Plugin/pushMidiInDropsPastCapacity") = []
{
    auto wrapper = makeWrapper<SynthPlugin>();
    auto accepted = 0;

    for (auto i = 0; i <= ProcessContext::defaultMidiCapacity; ++i)
        accepted += wrapper.pushMidiIn(0, MIDI::Event::noteOn(0, 60, 1.f)) ? 1 : 0;

    check(accepted == ProcessContext::defaultMidiCapacity);
};

auto tMidiOut = test("Plugin/midiOutIsClearedEveryBlockAndReadable") = []
{
    auto wrapper = makeWrapper<SynthPlugin>();
    auto& plugin = pluginOf<SynthPlugin>(wrapper);

    wrapper.pushMidiIn(0, MIDI::Event::noteOn(0, 60, 1.f));
    wrapper.pushMidiIn(0, MIDI::Event::noteOn(0, 64, 1.f));
    wrapper.process();

    check(plugin.midiOutWasEmpty);
    check(wrapper.midiOut().size() == 1);
    check(wrapper.midiOut()[0].size() == 2);

    // The adapter forgot clearMidi(): the in bus still holds both events, but the
    // out bus starts empty anyway.
    wrapper.process();
    check(plugin.midiOutWasEmpty);
    check(wrapper.midiOut()[0].size() == 2);

    wrapper.clearMidi();
    wrapper.process();
    check(wrapper.midiOut()[0].empty());
};

auto tReset = test("Plugin/resetReachesThePlugin") = []
{
    auto wrapper = makeWrapper();
    wrapper.reset();
    wrapper.reset();
    check(pluginOf<GainPlugin>(wrapper).resets == 2);
};

auto tPlayhead = test("Plugin/thePlayheadReachesTheContext") = []
{
    auto wrapper = makeWrapper();
    auto playhead = Playhead {};
    playhead.isValid = true;
    playhead.isPlaying = true;
    playhead.bpm = 97.0;
    playhead.sampleTime = 4800;

    wrapper.setPlayhead(playhead);
    wrapper.process();

    auto& seen = pluginOf<GainPlugin>(wrapper).playhead;
    check(seen.isValid && seen.isPlaying);
    check(seen.bpm == 97.0);
    check(seen.sampleTime == 4800);
};

auto tDefaults = test("Plugin/latencyAndTailDefaultToNone") = []
{
    auto wrapper = makeWrapper();
    check(wrapper.plugin().latencySamples() == 0);
    check(wrapper.plugin().tailSamples() == 0);
    check(!wrapper.plugin().takeLatencyChanged());
};

auto tFormat = test("Plugin/theFormatIsVisibleFromPrepareOn") = []
{
    auto wrapper = makeWrapper(PluginFormat::VST3);
    auto& plugin = pluginOf<GainPlugin>(wrapper);
    wrapper.prepare(44100, blockSize);

    check(plugin.formatAtConstruction == PluginFormat::Unknown);
    check(plugin.formatAtPrepare == PluginFormat::VST3);
    check(plugin.format() == PluginFormat::VST3);
};

auto tListener = test("Plugin/hostNotificationsReachTheListener") = []
{
    auto wrapper = makeWrapper();
    auto& plugin = wrapper.plugin();

    plugin.notifyHostLatencyChanged();

    auto listener = RecordingListener {};
    plugin.setHostEditListener(&listener);
    plugin.notifyHostLatencyChanged();
    plugin.notifyHostParameterInfoChanged();
    plugin.notifyHostParameterInfoChanged();

    check(plugin.hostEditListener() == &listener);
    check(listener.latency == 1);
    check(listener.info == 2);
};
} // namespace
