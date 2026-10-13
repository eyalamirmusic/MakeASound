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
#include <vector>

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
         .create = []
         {
             return TestPlugins::Module::create<TestPlugins::GainPlugin>(
                 TestPlugins::Module::gainStandIn);
         }});
    module.plugins.add(
        {.name = "Echo",
         .category = Category::Instrument,
         .pluginCode = "Echo",
         .create = []
         {
             return TestPlugins::Module::create<TestPlugins::SynthPlugin>(
                 TestPlugins::Module::echoStandIn);
         }});
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
    {
        throw std::runtime_error("no document");
    }
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
{
    return std::abs(a - b) < 1e-4f;
}

template <typename T = GainPlugin>
PluginWrapper makeWrapper(PluginFormat format = PluginFormat::Unknown)
{
    return PluginWrapper(EA::makeOwned<T>(), format);
}

template <typename T>
T& pluginOf(PluginWrapper& wrapper)
{
    return static_cast<T&>(wrapper.plugin());
}

Block ramp(float first)
{
    auto block = Block {};

    for (auto i = 0; i < blockSize; ++i)
        block[i] = first + static_cast<float>(i);

    return block;
}

bool equals(const Block& block, const Block& expected)
{
    return block == expected;
}

bool isSilent(const Block& block)
{
    for (auto sample: block)
        if (sample != 0.f)
            return false;

    return true;
}

template <typename Predicate>
bool pumpUntil(Predicate ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {5000});
}

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

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
    check(module.plugins.size() == 2);

    auto gain = PluginWrapper(module.plugins[0].create());
    check(gain.plugin().name() == "Gain");

    auto echo = PluginWrapper(module.plugins[1].create());
    check(echo.plugin().name() == "Synth");
    check(Module::lastCreated == &echo.plugin());
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

auto tTrackInfo = test("Plugin/trackInfoReachesThePluginOnTheMessageThread") = []
{
    auto wrapper = makeWrapper();
    auto& plugin = pluginOf<GainPlugin>(wrapper);
    check(!plugin.trackInfo().isValid);
    check(plugin.trackInfoChanges == 0);

    wrapper.setTrackInfo({.isValid = true, .name = "Lead Vocal", .index = 3});
    check(plugin.trackInfoChanges == 1);
    check(plugin.trackInfo().name == "Lead Vocal");
    check(plugin.trackInfo().index == 3);
    check(plugin.trackInfoHeard == plugin.trackInfo());

    // Off the message thread it is queued there.
    auto host = std::thread(
        [&] { wrapper.setTrackInfo({.isValid = true, .name = "Bass"}); });
    host.join();
    check(plugin.trackInfo().name == "Lead Vocal");

    check(pumpUntil([&] { return plugin.trackInfoChanges == 2; }));
    check(plugin.trackInfo().name == "Bass");
    check(plugin.trackInfo().index == 0);
};

auto tTrackInfoPastTeardown = test("Plugin/trackInfoQueuedPastTeardownDrops") = []
{
    {
        auto wrapper = makeWrapper();
        auto host = std::thread(
            [&] { wrapper.setTrackInfo({.isValid = true, .name = "Gone"}); });
        host.join();
    }

    // The queued call finds the wrapper gone and does nothing.
    eacp::Threads::runEventLoopFor(eacp::Time::MS {100});
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

// The bypass PluginWrapper adds to an effect that declares none.

void setBypass(PluginWrapper& wrapper, bool on)
{
    wrapper.setNormalizedParameter(wrapper.plugin().parameters().bypassIndex(),
                                   on ? 1.f : 0.f);
}

bool isBypassed(const PluginWrapper& wrapper)
{
    return wrapper.getParameter(wrapper.plugin().parameters().bypassIndex()) > 0.5f;
}

// The host side of a LatentEffect block: separate main buffers or one shared
// pair, and an aux output that starts every block holding -3.
struct LatentHost
{
    void block(PluginWrapper& wrapper, bool inPlace = false)
    {
        if (inPlace)
            out = in;

        const float* inputs[] = {in[0].data(), in[1].data()};
        float* outputs[] = {out[0].data(), out[1].data()};
        float* auxes[] = {aux[0].data(), aux[1].data()};
        const float* const* source = inPlace ? outputs : inputs;

        aux[0].fill(-3.f);
        aux[1].fill(-3.f);

        wrapper.bindInput(0, source, 2, blockSize);
        wrapper.bindOutput(0, outputs, 2, blockSize, source, 2);
        wrapper.bindOutput(1, auxes, 2, blockSize);
        wrapper.process();
    }

    std::array<Block, 2> in {};
    std::array<Block, 2> out {};
    std::array<Block, 2> aux {};
};

Block filled(float value)
{
    auto block = Block {};
    block.fill(value);
    return block;
}

// A plugin that saves nothing, recording the document its load is handed.
struct StatelessEffect : Plugin
{
    std::string_view name() const override { return "Stateless"; }
    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    void loadState(std::string_view data, StateContext) override
    {
        loaded = std::string(data);
    }

    std::string loaded = "unset";
};

// A plugin that sets its parameter list a second time, after the wrapper.
struct RebuildingEffect : Plugin
{
    RebuildingEffect() { rebuild(); }

    void rebuild() { setParameters(ParameterList {params}); }

    std::string_view name() const override { return "Rebuilding"; }
    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    LatentParams params;
};

// A bypass of its own: every output 3.
struct OwnBypassEffect : LatentEffect
{
    void processBypassed(ProcessContext& context) noexcept override
    {
        for (auto& output: context.outputs)
            output.fill(3.f);
    }
};

auto tAddedBypass = test("Plugin/anEffectWithoutABypassGetsOneOfItsOwn") = []
{
    auto fresh = LatentEffect {};
    check(fresh.parameters().bypassIndex() < 0);
    check(PluginWrapper::addsBypass(fresh));

    auto wrapper = makeWrapper<LatentEffect>();
    const auto& list = wrapper.plugin().parameters();
    auto index = list.bypassIndex();

    check(list.size() == 2);
    check(list.indexOf("Gain") == 0);
    check(index == 1);

    const auto& entry = list.entry(index);
    check(entry.id == "__bypass");
    check(entry.displayName == "Bypass");
    check(entry.bypass);
    check(entry.sessionOnly);
    check(entry.groups.empty());
    check(list.isHostExposed(index));
    check(list[index].isBypass());
    check(list[index].numSteps() == 1);
    check(entry.hostId == hostIdFor("__bypass"));
    check(list.indexOfHostId(entry.hostId) == index);

    // Clear of the ids VST3's MIDI controller shadow parameters use.
    check((entry.hostId & 0x7fff0000u) != 0x7fff0000u);

    check(!isBypassed(wrapper));
    wrapper.holdParameter(index);
    check(wrapper.isParameterHeld(index));
};

auto tNoAddedBypass = test("Plugin/aPluginWithABypassOrNoInputGetsNoneAdded") = []
{
    auto gain = makeWrapper<GainPlugin>();
    auto& params = pluginOf<GainPlugin>(gain).params;
    check(gain.plugin().parameters().indexOf("__bypass") < 0);
    check(gain.plugin().parameters().bypassIndex()
          == gain.plugin().parameters().indexOf(&params.bypass));
    check(!PluginWrapper::addsBypass(gain.plugin()));

    auto synth = makeWrapper<SynthPlugin>();
    check(synth.plugin().parameters().indexOf("__bypass") < 0);
    check(synth.plugin().parameters().bypassIndex() < 0);
    check(synth.plugin().parameters().size() == 1);
    check(!PluginWrapper::addsBypass(synth.plugin()));
};

// An effect that declines the added bypass.
struct NoHostBypassEffect : LatentEffect
{
    bool wantsHostBypass() const override { return false; }
};

auto tBypassOptOut = test("Plugin/anEffectCanDeclineTheAddedBypass") = []
{
    auto fresh = NoHostBypassEffect {};
    check(!PluginWrapper::addsBypass(fresh));

    auto wrapper = makeWrapper<NoHostBypassEffect>();
    const auto& list = wrapper.plugin().parameters();

    check(list.size() == 1);
    check(list.bypassIndex() < 0);
    check(list.indexOf("__bypass") < 0);
    check(!contains(wrapper.saveState(StateContext::Session), "__bypass"));
};

auto tBypassDelayed = test("Plugin/theAddedBypassDelaysTheInputByTheLatency") = []
{
    auto wrapper = makeWrapper<LatentEffect>();
    auto& plugin = pluginOf<LatentEffect>(wrapper);
    plugin.latency = 5;
    setBypass(wrapper, true);
    wrapper.prepare(48000, blockSize);

    auto host = LatentHost {};
    host.in = {ramp(1.f), ramp(100.f)};
    host.block(wrapper);

    check(plugin.processed == 0);
    check(plugin.bypassed == 1);

    for (auto i = 0; i < blockSize; ++i)
    {
        check(host.out[0][i] == (i < 5 ? 0.f : 1.f + static_cast<float>(i - 5)));
        check(host.out[1][i] == (i < 5 ? 0.f : 100.f + static_cast<float>(i - 5)));
    }

    check(isSilent(host.aux[0]));
    check(isSilent(host.aux[1]));

    host.in = {ramp(1000.f), ramp(2000.f)};
    host.block(wrapper);

    for (auto i = 0; i < 5; ++i)
        check(host.out[0][i] == static_cast<float>(blockSize - 5 + i + 1));

    check(host.out[0][5] == 1000.f);
    check(host.out[1][5] == 2000.f);
};

auto tBypassInPlace = test("Plugin/theAddedBypassDelaysInPlace") = []
{
    auto wrapper = makeWrapper<LatentEffect>();
    pluginOf<LatentEffect>(wrapper).latency = 3;
    setBypass(wrapper, true);
    wrapper.prepare(48000, blockSize);

    auto host = LatentHost {};
    host.in = {ramp(1.f), ramp(100.f)};
    host.block(wrapper, true);

    check(host.out[0][2] == 0.f);
    check(host.out[0][3] == 1.f);
    check(host.out[1][blockSize - 1] == 100.f + static_cast<float>(blockSize - 4));
};

auto tBypassMidi = test("Plugin/theAddedBypassPassesMidiThrough") = []
{
    auto wrapper = makeWrapper<LatentEffect>();
    wrapper.prepare(48000, blockSize);
    auto host = LatentHost {};

    auto sendNotes = [&]
    {
        wrapper.clearMidi();
        wrapper.pushMidiIn(0, MIDI::Event::noteOn(0, 60, 1.f, 9));
        wrapper.pushMidiIn(0, MIDI::Event::noteOff(0, 60, 0.f, 3));
        wrapper.sortMidiInByOffset();
        host.block(wrapper);
    };

    sendNotes();
    check(wrapper.midiOut()[0].empty());

    setBypass(wrapper, true);
    wrapper.reset();
    sendNotes();

    const auto& out = wrapper.midiOut()[0];
    check(out.size() == 2);
    check(out[0].sampleOffset == 3);
    check(out[1].sampleOffset == 9);
};

auto tBypassFades = test("Plugin/togglingTheAddedBypassCrossfadesAndResumes") = []
{
    auto wrapper = makeWrapper<LatentEffect>();
    auto& plugin = pluginOf<LatentEffect>(wrapper);
    setBypass(wrapper, true);
    wrapper.prepare(48000, blockSize);

    auto host = LatentHost {};
    host.in = {filled(1.f), filled(1.f)};
    host.block(wrapper);
    check(host.out[0] == filled(1.f));
    check(plugin.processed == 0);

    // 10 ms at 48 kHz is 480 samples: the first block starts the fade, both
    // sides running, and the eighth ends it.
    setBypass(wrapper, false);
    host.block(wrapper);
    check(plugin.processed == 1);
    check(plugin.bypassed == 2);
    check(host.out[0][0] > 1.f && host.out[0][0] < 1.01f);

    for (auto i = 1; i < blockSize; ++i)
        check(host.out[0][i] > host.out[0][i - 1]);

    check(host.out[0][blockSize - 1] < 2.f);
    check(host.aux[0][blockSize - 1] > 0.f && host.aux[0][blockSize - 1] < 7.f);

    for (auto b = 0; b < 7; ++b)
        host.block(wrapper);

    check(host.out[0][blockSize - 1] == 2.f);
    check(plugin.bypassed == 9);

    host.block(wrapper);
    check(host.out[0] == filled(2.f));
    check(host.aux[0] == filled(7.f));
    check(plugin.bypassed == 9);

    setBypass(wrapper, true);
    host.block(wrapper);
    check(host.out[0][0] < 2.f && host.out[0][0] > 1.99f);
    check(host.out[0][blockSize - 1] > 1.f);

    for (auto b = 0; b < 8; ++b)
        host.block(wrapper);

    check(host.out[0] == filled(1.f));
    check(isSilent(host.aux[0]));
    check(plugin.processed == 9 + 1 + 8 - 1);
};

auto tBypassReset = test("Plugin/resetEndsAnAddedBypassFade") = []
{
    auto wrapper = makeWrapper<LatentEffect>();
    auto& plugin = pluginOf<LatentEffect>(wrapper);
    wrapper.prepare(48000, blockSize);

    auto host = LatentHost {};
    host.in = {filled(1.f), filled(1.f)};
    host.block(wrapper);

    setBypass(wrapper, true);
    wrapper.reset();
    host.block(wrapper);

    check(host.out[0] == filled(1.f));
    check(plugin.processed == 1);
};

auto tBypassSession = test("Plugin/theAddedBypassIsSavedInASessionOnly") = []
{
    auto source = makeWrapper<LatentEffect>();
    setBypass(source, true);
    source.setParameter(0, 3.f);

    auto session = source.saveState(StateContext::Session);
    auto preset = source.saveState(StateContext::Preset);
    check(contains(session, "__bypass"));
    check(!contains(preset, "__bypass"));

    auto target = makeWrapper<LatentEffect>();
    target.loadState(session, StateContext::Session);
    check(isBypassed(target));
    check(near(target.getParameter(0), 3.f));

    target.loadState(preset, StateContext::Preset);
    check(isBypassed(target));

    // A session from before the key, or from a plugin that had none, means off.
    target.loadState(preset, StateContext::Session);
    check(!isBypassed(target));

    // Loaded off the message thread like any parameter.
    loadOffMessageThread(target, session);
    check(isBypassed(target));
};

auto tBypassStateless = test("Plugin/aStatelessEffectKeepsItsBypassUnseen") = []
{
    auto source = makeWrapper<StatelessEffect>();
    setBypass(source, true);

    auto session = source.saveState(StateContext::Session);
    check(contains(session, "__bypass"));
    check(source.saveState(StateContext::Preset).empty());

    auto target = makeWrapper<StatelessEffect>();
    target.loadState(session, StateContext::Session);

    check(isBypassed(target));
    check(pluginOf<StatelessEffect>(target).loaded.empty());
};

auto tBypassStripped = test("Plugin/thePluginNeverSeesTheAddedBypassKey") = []
{
    auto wrapper = makeWrapper<StatelessEffect>();
    auto& plugin = pluginOf<StatelessEffect>(wrapper);

    wrapper.loadState(R"({"__bypass": true, "x": 1})", StateContext::Session);
    check(isBypassed(wrapper));
    check(contains(plugin.loaded, "\"x\""));
    check(!contains(plugin.loaded, "__bypass"));

    // A preset's key is taken out too, and moves nothing.
    wrapper.loadState(R"({"__bypass": false, "x": 2})", StateContext::Preset);
    check(isBypassed(wrapper));
    check(!contains(plugin.loaded, "__bypass"));

    // Not JSON: handed over untouched.
    wrapper.loadState("raw bytes", StateContext::Session);
    check(plugin.loaded == "raw bytes");
    check(isBypassed(wrapper));
};

auto tBypassRebuilt =
    test("Plugin/settingTheParametersAgainKeepsTheAddedBypass") = []
{
    auto wrapper = makeWrapper<RebuildingEffect>();
    auto& plugin = pluginOf<RebuildingEffect>(wrapper);
    plugin.rebuild();

    const auto& list = plugin.parameters();
    check(list.size() == 2);
    check(list.bypassIndex() == 1);
    check(list.entry(1).id == "__bypass");

    setBypass(wrapper, true);
    check(isBypassed(wrapper));
};

auto tBypassOverride = test("Plugin/processBypassedCanBeOverridden") = []
{
    auto wrapper = makeWrapper<OwnBypassEffect>();
    setBypass(wrapper, true);
    wrapper.prepare(48000, blockSize);

    auto host = LatentHost {};
    host.in = {ramp(1.f), ramp(100.f)};
    host.block(wrapper);

    check(host.out[0] == filled(3.f));
    check(host.aux[1] == filled(3.f));
};

auto tBypassGrows = test("Plugin/aLatencyThatGrowsWidensTheAddedBypassDelay") = []
{
    auto wrapper = makeWrapper<LatentEffect>();
    auto& plugin = pluginOf<LatentEffect>(wrapper);
    plugin.latency = 2;
    setBypass(wrapper, true);
    wrapper.prepare(48000, blockSize);

    plugin.latency = 200;
    plugin.notifyHostLatencyChanged();

    auto host = LatentHost {};
    auto heard = std::vector<float>();

    for (auto b = 0; b < 5; ++b)
    {
        host.in[0] = ramp(1.f + static_cast<float>(b * blockSize));
        host.block(wrapper);
        heard.insert(heard.end(), host.out[0].begin(), host.out[0].end());
    }

    for (auto k = 0; k < static_cast<int>(heard.size()); ++k)
        check(heard[k] == (k < 200 ? 0.f : 1.f + static_cast<float>(k - 200)));
};

// A latency that moved with no notification (a parameter, say) is caught by the
// adapter's latency check, which goes through the wrapper.
auto tBypassChecked = test("Plugin/theLatencyCheckWidensTheAddedBypassDelay") = []
{
    auto wrapper = makeWrapper<LatentEffect>();
    auto& plugin = pluginOf<LatentEffect>(wrapper);
    setBypass(wrapper, true);
    wrapper.prepare(48000, blockSize);

    plugin.latency = 150;
    check(!wrapper.takeLatencyChanged());

    auto host = LatentHost {};
    auto heard = std::vector<float>();

    for (auto b = 0; b < 4; ++b)
    {
        host.in[0] = ramp(1.f + static_cast<float>(b * blockSize));
        host.block(wrapper);
        heard.insert(heard.end(), host.out[0].begin(), host.out[0].end());
    }

    for (auto k = 0; k < static_cast<int>(heard.size()); ++k)
        check(heard[k] == (k < 150 ? 0.f : 1.f + static_cast<float>(k - 150)));
};

// A port of a JUCE effect: its APVTS was "Legacy" with `cutoff` and `mode`, and
// it kept a `preset` property on the root.
struct LegacyParams : ParameterGroup
{
    LegacyParams() { add(cutoff, mode); }

    FloatParam cutoff {"Cutoff", 20.f, 20000.f, 1000.f, {.id = "cutoff"}};
    ChoiceParam mode {"Mode", {"Clean", "Warm", "Hot"}, 0, {.id = "mode"}};
};

struct LegacyOwnBypassParams : ParameterGroup
{
    LegacyOwnBypassParams() { add(cutoff, mode, bypass); }

    FloatParam cutoff {"Cutoff", 20.f, 20000.f, 1000.f, {.id = "cutoff"}};
    ChoiceParam mode {"Mode", {"Clean", "Warm", "Hot"}, 0, {.id = "mode"}};
    BoolParam bypass {"Bypass", false, {.id = "bypass", .bypass = true}};
};

std::atomic<int> legacyLoadsEver {0};

template <typename ParamsT>
struct LegacyEffectOf : StatePlugin<GainStateOf<ParamsT>>
{
    std::string_view name() const override { return "Legacy"; }
    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    bool loadLegacy(const ForeignState& foreign) override
    {
        ++legacyLoads;
        ++legacyLoadsEver;
        lastOrigin = foreign.origin;
        lastBypass = foreign.bypass;

        auto root = Compat::loadJuceApvtsBlob(
            foreign.bytes, this->params, {.rootType = "Legacy"});

        if (!root)
            return false;

        if (const auto* preset = root->property("preset"))
            this->state.preset = *preset;

        return true;
    }

    int legacyLoads = 0;
    ForeignOrigin lastOrigin = ForeignOrigin::JuceVST3;
    std::optional<bool> lastBypass;
};

using LegacyEffect = LegacyEffectOf<LegacyParams>;
using LegacyOwnBypassEffect = LegacyEffectOf<LegacyOwnBypassParams>;

struct NotificationCounter : HostEditListener
{
    void beginParameterEdit(int) noexcept override {}
    void performParameterEdit(int, float) noexcept override {}
    void endParameterEdit(int) noexcept override {}
    void parameterValuesChanged() noexcept override { ++values; }
    void stateChanged() noexcept override { ++states; }

    std::atomic<int> values {0};
    int states = 0;
};

// copyXmlToBinary's framing around a single-line XML.
std::string juceBlob(std::string_view xml)
{
    auto blob = std::string {"VC2!"};
    auto size = static_cast<uint32_t>(xml.size());

    for (auto i = 0; i < 4; ++i)
        blob += static_cast<char>((size >> (8 * i)) & 0xff);

    blob += xml;
    blob += '\0';
    return blob;
}

const auto legacyBlob =
    juceBlob("<Legacy preset=\"Old\"><PARAM id=\"cutoff\" value=\"440.0\"/>"
             "<PARAM id=\"mode\" value=\"2.0\"/></Legacy>");

ForeignState foreignState(std::string_view bytes,
                          std::optional<bool> bypass = std::nullopt,
                          StateContext context = StateContext::Session)
{
    auto state = ForeignState {};
    state.origin = ForeignOrigin::JuceAU;
    state.bytes = bytes;
    state.context = context;
    state.bypass = bypass;
    return state;
}

std::string legacyDocument(float cutoff, const std::string& preset)
{
    auto source = makeWrapper<LegacyEffect>();
    source.setParameter(0, cutoff);
    pluginOf<LegacyEffect>(source).state.preset = preset;
    pluginOf<LegacyEffect>(source).state.markChanged();
    return source.saveState(StateContext::Session);
}

auto tForeignDefault = test("Plugin/aPluginTakesNoForeignStateByDefault") = []
{
    auto wrapper = makeWrapper();
    check(!wrapper.loadForeignState(foreignState(legacyBlob, true)));
    check(!isBypassed(wrapper));

    auto plain = PluginWrapper(EA::makeOwned<ThrowingPlugin>());
    check(!plain.loadForeignState(foreignState(legacyBlob)));
};

auto tForeignInline = test("Plugin/aForeignStateLoadsAtOnceOnTheMessageThread") = []
{
    auto wrapper = makeWrapper<LegacyEffect>();
    auto& plugin = pluginOf<LegacyEffect>(wrapper);
    auto listener = NotificationCounter {};
    plugin.setHostEditListener(&listener);

    auto state = foreignState(legacyBlob, true);
    state.origin = ForeignOrigin::JuceAAX;
    check(wrapper.loadForeignState(state));

    check(plugin.legacyLoads == 1);
    check(plugin.lastOrigin == ForeignOrigin::JuceAAX);
    check(plugin.lastBypass == std::optional {true});
    check(near(plugin.params.cutoff.get(), 440.f));
    check(plugin.params.mode.getIndex() == 2);
    check(plugin.state.preset == "Old");
    check(isBypassed(wrapper));

    // Republished, and a load the host asked for: no dirty mark, and the
    // values are the adapter's to re-read on its own thread.
    check(plugin.isStateSnapshotCurrent());
    check(contains(wrapper.saveState(StateContext::Session), "Old"));
    check(listener.states == 0);
    check(listener.values == 0);

    plugin.setHostEditListener(nullptr);
};

auto tForeignRefused = test("Plugin/aRefusedForeignStateLeavesTheBypass") = []
{
    auto wrapper = makeWrapper<LegacyEffect>();
    auto& plugin = pluginOf<LegacyEffect>(wrapper);

    check(!wrapper.loadForeignState(foreignState("not a JUCE state", true)));
    check(plugin.legacyLoads == 1);
    check(!isBypassed(wrapper));

    auto otherRoot =
        juceBlob("<Other><PARAM id=\"cutoff\" value=\"440.0\"/></Other>");
    check(!wrapper.loadForeignState(foreignState(otherRoot, true)));
    check(near(plugin.params.cutoff.get(), 1000.f));
};

auto tForeignPreset = test("Plugin/aForeignPresetLeavesTheAddedBypass") = []
{
    auto wrapper = makeWrapper<LegacyEffect>();

    check(wrapper.loadForeignState(
        foreignState(legacyBlob, true, StateContext::Preset)));
    check(near(pluginOf<LegacyEffect>(wrapper).params.cutoff.get(), 440.f));
    check(!isBypassed(wrapper));

    // A state that carries no bypass leaves it where it was, as JUCE does.
    setBypass(wrapper, true);
    check(wrapper.loadForeignState(foreignState(legacyBlob)));
    check(isBypassed(wrapper));
};

auto tForeignOwnBypass =
    test("Plugin/aForeignBypassIsThePluginsWhenItHasItsOwn") = []
{
    auto wrapper = makeWrapper<LegacyOwnBypassEffect>();
    auto& plugin = pluginOf<LegacyOwnBypassEffect>(wrapper);
    check(!PluginWrapper::addsBypass(plugin));

    check(wrapper.loadForeignState(foreignState(legacyBlob, true)));
    check(plugin.lastBypass == std::optional {true});
    check(!plugin.params.bypass.isOn());
};

auto tForeignDeferred = test("Plugin/aForeignStateFromAnotherThreadLoadsLater") = []
{
    auto wrapper = makeWrapper<LegacyEffect>();
    auto& plugin = pluginOf<LegacyEffect>(wrapper);
    auto listener = NotificationCounter {};
    plugin.setHostEditListener(&listener);

    auto taken = false;

    {
        // The bytes go before the load runs: the wrapper keeps its own copy.
        auto bytes = std::string {legacyBlob};
        auto host = std::thread(
            [&] { taken = wrapper.loadForeignState(foreignState(bytes, true)); });
        host.join();
    }

    check(taken);
    check(plugin.legacyLoads == 0);
    check(near(plugin.params.cutoff.get(), 1000.f));

    check(pumpUntil([&] { return plugin.legacyLoads == 1; }));
    check(near(plugin.params.cutoff.get(), 440.f));
    check(plugin.state.preset == "Old");
    check(isBypassed(wrapper));
    check(listener.values == 1);
    check(listener.states == 0);

    plugin.setHostEditListener(nullptr);
};

auto tForeignThenNative =
    test("Plugin/aLaterNativeLoadDropsAQueuedForeignState") = []
{
    auto document = legacyDocument(100.f, "Native");
    auto wrapper = makeWrapper<LegacyEffect>();
    auto& plugin = pluginOf<LegacyEffect>(wrapper);

    auto host =
        std::thread([&] { wrapper.loadForeignState(foreignState(legacyBlob)); });
    host.join();

    wrapper.loadState(document, StateContext::Session);
    eacp::Threads::runEventLoopFor(eacp::Time::MS {100});

    check(plugin.legacyLoads == 0);
    check(near(plugin.params.cutoff.get(), 100.f));
    check(plugin.state.preset == "Native");
};

auto tNativeThenForeign = test("Plugin/aForeignStateDropsAQueuedNativeLoad") = []
{
    auto document = legacyDocument(100.f, "Native");
    auto wrapper = makeWrapper<LegacyEffect>();
    auto& plugin = pluginOf<LegacyEffect>(wrapper);

    auto host =
        std::thread([&] { wrapper.loadState(document, StateContext::Session); });
    host.join();
    check(near(plugin.params.cutoff.get(), 100.f));

    check(wrapper.loadForeignState(foreignState(legacyBlob)));
    eacp::Threads::runEventLoopFor(eacp::Time::MS {100});

    check(near(plugin.params.cutoff.get(), 440.f));
    check(plugin.state.preset == "Old");
};

auto tForeignTwice = test("Plugin/onlyTheLatestQueuedForeignStateRuns") = []
{
    const auto second = juceBlob(
        "<Legacy preset=\"Second\"><PARAM id=\"cutoff\" value=\"220\"/></Legacy>");
    auto wrapper = makeWrapper<LegacyEffect>();
    auto& plugin = pluginOf<LegacyEffect>(wrapper);

    for (const auto* bytes: {&legacyBlob, &second})
    {
        auto host =
            std::thread([&] { wrapper.loadForeignState(foreignState(*bytes)); });
        host.join();
    }

    check(pumpUntil([&] { return plugin.state.preset == "Second"; }));
    eacp::Threads::runEventLoopFor(eacp::Time::MS {50});

    check(plugin.legacyLoads == 1);
    check(near(plugin.params.cutoff.get(), 220.f));
};

auto tForeignTeardown = test("Plugin/aForeignStateQueuedPastTeardownDrops") = []
{
    legacyLoadsEver = 0;

    {
        auto wrapper = makeWrapper<LegacyEffect>();
        auto host =
            std::thread([&] { wrapper.loadForeignState(foreignState(legacyBlob)); });
        host.join();
    }

    eacp::Threads::runEventLoopFor(eacp::Time::MS {100});
    check(legacyLoadsEver == 0);
};

// A plugin whose parameter is renamed at run time, as a hosted plugin's is.
struct RenamingPlugin : Plugin
{
    struct Param : FloatParam
    {
        using FloatParam::FloatParam;
        using FloatParam::setDisplayInfo;
    };

    struct Params : ParameterGroup
    {
        Params() { add(level); }
        Param level {"Level", 0.f, 1.f, 0.5f};
    };

    RenamingPlugin() { setParameters(ParameterList {params}); }

    std::string_view name() const override { return "Renaming"; }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    void rename(std::string_view to)
    {
        params.level.setDisplayInfo(to, {}, {});
        refreshParameterNames();
    }

    Params params;
};

struct InfoCounter : HostEditListener
{
    void beginParameterEdit(int) noexcept override {}
    void performParameterEdit(int, float) noexcept override {}
    void endParameterEdit(int) noexcept override {}
    void parameterInfoChanged() noexcept override { ++infos; }

    int infos = 0;
};

auto tRenameNotifies = test("Plugin/aRenameTellsTheHostOnlyWhenANameChanged") = []
{
    auto plugin = RenamingPlugin {};
    auto listener = InfoCounter {};
    plugin.setHostEditListener(&listener);

    plugin.rename("Volume");
    check(plugin.parameters().entry(0).displayName == "Volume");
    check(listener.infos == 1);

    plugin.rename("Volume");
    check(listener.infos == 1);

    plugin.setHostEditListener(nullptr);
};
} // namespace
