// The standalone format under the allocation ban: a whole StandaloneProcessor
// block on Engine, driven by hand and by the default output device's live
// callback.

#include "AllocationProbe.h"
#include "TestPlugins.h"

#include <MakeASound/Plugin/Standalone/StandaloneProcessor.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <chrono>
#include <thread>
#include <type_traits>

using namespace nano;
using namespace TestPlugins;
using Probe::allocationsIn;

namespace
{
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
    auto wrapper = PluginWrapper(EA::makeOwned<PluginT>(), PluginFormat::Standalone);
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

auto tStandaloneEffect = test("Allocations/standaloneEffectBlockIsOffTheHeap") = []
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

auto tStandaloneLive = test("Allocations/standaloneLiveCallbackIsOffTheHeap") = []
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
} // namespace
