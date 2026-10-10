// The plugin core's audio-thread paths under the allocation ban: parameter value
// I/O, host-id lookup, the realtime swap's exchange, and a whole PluginWrapper
// block as an adapter drives it, for an effect and an instrument, in place and
// not; and with the standalone format, a whole StandaloneProcessor block on
// Engine, driven by hand and by the default output device's live callback.

#include "AllocationProbe.h"
#include "TestPlugins.h"

#if MAKEASOUND_HAS_GUI
#include <MakeASound/Plugin/Standalone/StandaloneProcessor.h>
#endif

#include <NanoTest/NanoTest.h>

#include <array>
#include <chrono>
#include <thread>
#include <type_traits>

using namespace nano;
using namespace TestPlugins;
using Probe::allocationsIn;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
auto tSwapExchange = test("Allocations/realtimeSwapExchangeIsOffTheHeap") = []
{
    auto swap = RealtimeSwap<std::array<float, 1024>>();
    swap.publish();
    swap.currentForBlock();
    swap.publish();

    auto* first = static_cast<std::array<float, 1024>*>(nullptr);
    auto* second = first;

    auto count = allocationsIn(
        [&]
        {
            first = swap.currentForBlock();
            second = swap.currentForBlock();
        });

    check(count == 0);
    check(first != nullptr);
    check(first == second);
};

struct AllocationParams : ParameterGroup
{
    AllocationParams() { add(gain, cutoff, mode, bypass); }

    DecibelParam gain {"Gain", -60.f, 12.f, 0.f};
    HzParam cutoff {"Cutoff", 20.f, 20000.f, 1000.f};
    ChoiceParam mode {"Mode", {"Low", "Band", "High"}, 1};
    BoolParam bypass {"Bypass", false};
};

auto tParameterValues = test("Allocations/parameterValueIoIsOffTheHeap") = []
{
    auto params = AllocationParams {};
    auto list = ParameterList {params};
    auto sum = 0.f;

    auto count = allocationsIn(
        [&]
        {
            for (auto i = 0; i < list.size(); ++i)
            {
                auto& param = list[i];
                param.setValue(param.getValue());
                param.setNormalized(0.3f);
                sum += param.getNormalized();
                sum += param.toPlain(param.toNormalized(param.getValue()));
            }

            sum += params.gain.gain() + params.cutoff.get();
            sum += static_cast<float>(params.mode.getIndex());
            sum += params.bypass.isOn() ? 1.f : 0.f;
        });

    check(count == 0);
    check(sum > 0.f);
};

auto tHostIdLookup = test("Allocations/parameterHostIdLookupIsOffTheHeap") = []
{
    auto params = AllocationParams {};
    auto list = ParameterList {params};
    auto found = 0;

    auto count = allocationsIn(
        [&]
        {
            for (const auto& entry: list)
                found += list.indexOfHostId(entry.hostId) >= 0 ? 1 : 0;

            found += list.indexOfHostId(7u) < 0 ? 1 : 0;
        });

    check(count == 0);
    check(found == list.size() + 1);
};

struct Host
{
    Block inLeft = {};
    Block inRight = {};
    Block outLeft = {};
    Block outRight = {};

    std::array<const float*, 2> inputs {inLeft.data(), inRight.data()};
    std::array<float*, 2> outputs {outLeft.data(), outRight.data()};

    // In place: the output table is the input's.
    std::array<float*, 2> shared {inLeft.data(), inRight.data()};
};

// One block the way an adapter drives it, twice, so the second is steady state.
// Counts the MIDI out events the adapter drained into `drained`.
int allocationsPerBlock(PluginWrapper& wrapper,
                        Host& host,
                        bool inPlace,
                        int& drained)
{
    auto hostId = wrapper.plugin().parameters().entry(0).hostId;
    auto capacity = ProcessContext::defaultMidiCapacity;
    auto playhead = Playhead {};
    playhead.isValid = true;

    auto* outputs = inPlace ? host.shared.data() : host.outputs.data();
    auto* inputs = inPlace ? host.shared.data() : host.inputs.data();

    auto block = [&]
    {
        wrapper.setNormalizedParameter(0, 0.7f);
        wrapper.setParameterByHostId(hostId, 0.6f);
        wrapper.setPlayhead(playhead);
        wrapper.clearMidi();

        for (auto i = 0; i < capacity; ++i)
            wrapper.pushMidiIn(0, MIDI::Event::noteOn(0, 60, 1.f, capacity - i));

        wrapper.sortMidiInByOffset();
        wrapper.bindInput(0, inputs, 2, blockSize);
        wrapper.bindOutput(0, outputs, 2, blockSize, inputs, 2);
        wrapper.process();

        drained = 0;

        for (const auto& bus: wrapper.midiOut())
            drained += bus.size();
    };

    wrapper.prepare(48000, blockSize);
    block();

    return allocationsIn(block);
}

auto tEffectBlock = test("Allocations/pluginWrapperEffectBlockIsOffTheHeap") = []
{
    for (auto inPlace: {false, true})
    {
        auto wrapper = PluginWrapper(EA::makeOwned<GainPlugin>());
        auto host = Host {};
        auto drained = -1;

        check(allocationsPerBlock(wrapper, host, inPlace, drained) == 0);
        check(static_cast<GainPlugin&>(wrapper.plugin()).processed == 2);
        check(drained == 0);
    }
};

auto tInstrumentBlock =
    test("Allocations/pluginWrapperInstrumentBlockIsOffTheHeap") = []
{
    for (auto inPlace: {false, true})
    {
        auto wrapper = PluginWrapper(EA::makeOwned<SynthPlugin>());
        auto host = Host {};
        auto drained = 0;

        check(allocationsPerBlock(wrapper, host, inPlace, drained) == 0);
        check(drained == ProcessContext::defaultMidiCapacity);
    }
};

auto tUnboundBlock = test("Allocations/pluginWrapperUnboundBlockIsOffTheHeap") = []
{
    auto wrapper = PluginWrapper(EA::makeOwned<GainPlugin>());
    auto host = Host {};
    auto drained = 0;
    auto held = wrapper.plugin().parameters().entry(0).hostId;

    allocationsPerBlock(wrapper, host, false, drained);
    wrapper.holdParameter(0);

    auto count = allocationsIn(
        [&]
        {
            wrapper.setParameterByHostId(held, 0.1f);
            wrapper.process();
        });

    check(count == 0);
    check(static_cast<GainPlugin&>(wrapper.plugin()).boundSamples == 0);
};

#if MAKEASOUND_HAS_GUI
// The callback a device would hand Engine: two inputs, two outputs, clean.
struct StandaloneStream
{
    Block inLeft = {};
    Block inRight = {};
    Block outLeft = {};
    Block outRight = {};

    std::array<float*, 2> inputs {inLeft.data(), inRight.data()};
    std::array<float*, 2> outputs {outLeft.data(), outRight.data()};

    AudioCallbackInfo info(bool dirty)
    {
        auto result = AudioCallbackInfo {};
        result.numInputs = 2;
        result.numOutputs = 2;
        result.inputChannels = inputs.data();
        result.outputChannels = outputs.data();
        result.numSamples = blockSize;
        result.sampleRate = 48000;
        result.maxBlockSize = blockSize;
        result.dirty = dirty;
        return result;
    }
};

template <typename PluginT>
int standaloneAllocationsPerBlock(bool inject, int& processed)
{
    auto devices = DeviceManager {};
    auto midi = MidiManager {};
    auto engine = Engine {devices, midi};
    auto sender = Standalone::MidiSender {midi};
    auto wrapper =
        PluginWrapper(EA::makeOwned<PluginT>(), PluginFormat::Standalone);
    auto processor = Standalone::StandaloneProcessor {wrapper, sender};
    auto stream = StandaloneStream {};

    engine.prepare(processor, 48000, blockSize);

    auto first = stream.info(true);
    engine.process(first);

    if (inject)
        processor.injectMidi(MIDI::Event::noteOn(0, 60, 1.f));

    auto block = stream.info(false);
    auto count = allocationsIn([&] { engine.process(block); });

    if constexpr (std::is_same_v<PluginT, SynthPlugin>)
        processed = static_cast<SynthPlugin&>(wrapper.plugin()).numEvents;
    else
        processed = static_cast<GainPlugin&>(wrapper.plugin()).processed;

    return count;
}

auto tStandaloneEffect =
    test("Allocations/standaloneEffectBlockIsOffTheHeap") = []
{
    auto processed = 0;

    check(standaloneAllocationsPerBlock<GainPlugin>(false, processed) == 0);
    check(processed == 2);
};

auto tStandaloneInstrument =
    test("Allocations/standaloneInstrumentBlockIsOffTheHeap") = []
{
    auto events = 0;

    // The injected note is echoed to the MIDI out bus and into the sender.
    check(standaloneAllocationsPerBlock<SynthPlugin>(true, events) == 0);
    check(events == 1);
};

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

bool waitForVisits(Probe::ThreadProbe& probe, int wanted, Clock::duration timeout)
{
    auto deadline = Clock::now() + timeout;

    while (probe.visits.load() < wanted)
    {
        if (Clock::now() > deadline)
            return false;

        std::this_thread::sleep_for(2ms);
    }

    return true;
}

// Only the watched thread can lift its own ban.
void releaseTheWatchedThread(Probe::ThreadProbe& probe)
{
    auto seen = probe.visits.load();

    probe.measuring = false;
    waitForVisits(probe, seen + 3, 1s);
}

struct ProbedGainPlugin : GainPlugin
{
    explicit ProbedGainPlugin(Probe::ThreadProbe& probeToUse)
        : probe(probeToUse)
    {
    }

    void process(ProcessContext& context) noexcept override
    {
        probe.mark();
        GainPlugin::process(context);
    }

    Probe::ThreadProbe& probe;
};

auto tStandaloneLive =
    test("Allocations/standaloneLiveCallbackIsOffTheHeap") = []
{
    // Declared first: the audio thread still carries the ban on its way down.
    auto probe = Probe::ThreadProbe {};
    auto devices = DeviceManager {};
    auto midi = MidiManager {};
    auto sender = Standalone::MidiSender {midi};
    auto wrapper = PluginWrapper(EA::makeOwned<ProbedGainPlugin>(probe),
                                 PluginFormat::Standalone);
    auto processor = Standalone::StandaloneProcessor {wrapper, sender};
    auto engine = Engine {devices, midi};

    auto config = devices.getDefaultOutputConfig();

    if (!config.output.has_value())
        return;

    if (engine.start(config, processor) != Error::NoError)
        return;

    auto ran = waitForVisits(probe, 64, 3s);

    releaseTheWatchedThread(probe);

    auto violations = probe.violations.load();
    auto blocks = probe.visits.load();

    engine.stop();

    // A device that opened but barely called back measured nothing.
    if (!ran && blocks < 20)
        return;

    check(violations == 0);
};
#endif
} // namespace
