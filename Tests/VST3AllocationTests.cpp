// The VST3 adapter's audio-thread paths under the allocation ban: a whole
// process() call with automation, notes, shadow-parameter CC and an echoed
// output, over varying block sizes and a reconfiguration; the controller's
// setParamNormalized; and the held path an editor gesture takes.

#include "AllocationProbe.h"
#include "VST3TestHost.h"

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <array>

using namespace nano;
using namespace TestPlugins;
using namespace VST3Host;
using Probe::allocationsIn;

namespace
{
std::vector<Vst::ParamID> exposedIds(MakeASound::VST3::Adapter& adapter)
{
    auto ids = std::vector<Vst::ParamID> {};
    const auto& list = adapter.plugin().parameters();

    for (auto i = 0; i < list.size(); ++i)
        if (list.isHostExposed(i))
            ids.push_back(list.entry(i).hostId);

    return ids;
}

Vst::Event note(bool on, int offset)
{
    auto event = Vst::Event {};
    event.sampleOffset = offset;
    event.type = on ? Vst::Event::kNoteOnEvent : Vst::Event::kNoteOffEvent;

    if (on)
        event.noteOn = {0, 60, 0.f, 0.8f, 0, -1};
    else
        event.noteOff = {0, 60, 0.8f, -1, 0.f};

    return event;
}

// Everything a busy host block carries, filled outside the measurement.
template <typename P>
void fillBusyBlock(AdapterHost<P>& host, int numSamples, bool inPlace, int block)
{
    host.clearQueues();

    auto last = std::max(0, numSamples - 1);
    auto value = block % 2 == 0 ? 0.3 : 0.7;

    for (auto id: exposedIds(*host.adapter))
    {
        host.addPoint(id, 0, 1.0 - value);
        host.addPoint(id, last, value);
    }

    auto modWheel = Vst::ParamID {};
    auto bend = Vst::ParamID {};

    if (host.adapter->getMidiControllerAssignment(0, 0, 1, modWheel)
            == Steinberg::kResultTrue
        && host.adapter->getMidiControllerAssignment(0, 0, Vst::kPitchBend, bend)
               == Steinberg::kResultTrue)
    {
        for (auto id: {modWheel, bend})
        {
            host.addPoint(id, 0, value);
            host.addPoint(id, last, 1.0 - value);
        }
    }

    host.addEvent(note(true, 0));
    host.addEvent(note(false, last));

    host.context = {};
    host.context.state = Vst::ProcessContext::kPlaying
                         | Vst::ProcessContext::kTempoValid
                         | Vst::ProcessContext::kProjectTimeMusicValid
                         | Vst::ProcessContext::kTimeSigValid;
    host.context.tempo = 120.0;
    host.context.projectTimeMusic = 0.5 * block;
    host.context.projectTimeSamples = block * maxBlock;
    host.context.timeSigNumerator = 4;
    host.context.timeSigDenominator = 4;

    host.fillBlock(numSamples, inPlace);
}

template <typename P>
int allocationsOverBlocks(AdapterHost<P>& host, bool inPlace, int maxSamples)
{
    auto count = 0;
    auto block = 0;

    fillBusyBlock(host, maxSamples, inPlace, block++);
    host.run();

    for (auto size: {64, 17, 1, 63, 0})
    {
        fillBusyBlock(host, std::min(size, maxSamples), inPlace, block++);
        count += allocationsIn([&] { host.run(); });
    }

    return count;
}

template <typename P>
void checkProcessIsOffTheHeap(bool inPlace)
{
    auto host = AdapterHost<P> {};

    check(allocationsOverBlocks(host, inPlace, maxBlock) == 0);

    host.configure(44100.0, 32);
    check(allocationsOverBlocks(host, inPlace, 32) == 0);
}

auto tEffectProcess = test("Allocations/vst3EffectProcessIsOffTheHeap") = []
{
    for (auto inPlace: {false, true})
        checkProcessIsOffTheHeap<GainPlugin>(inPlace);

    auto host = AdapterHost<GainPlugin> {};
    allocationsOverBlocks(host, false, maxBlock);
    check(host.plugin().processed > 0);
};

auto tInstrumentProcess = test("Allocations/vst3InstrumentProcessIsOffTheHeap") = []
{
    checkProcessIsOffTheHeap<SynthPlugin>(false);

    auto host = AdapterHost<SynthPlugin> {};
    fillBusyBlock(host, maxBlock, false, 0);
    host.run();
    check(host.outputEvents.getEventCount() > 0);
};

auto tSetParamNormalized =
    test("Allocations/vst3SetParamNormalizedIsOffTheHeap") = []
{
    auto effect = AdapterHost<GainPlugin> {};
    auto effectIds = exposedIds(*effect.adapter);

    auto instrument = AdapterHost<SynthPlugin> {};
    auto shadow = MakeASound::VST3::shadowIdFor(0, 0, 1);

    auto count = allocationsIn(
        [&]
        {
            for (auto value: {0.25, 0.75})
            {
                for (auto id: effectIds)
                    effect.adapter->setParamNormalized(id, value);

                instrument.adapter->setParamNormalized(shadow, value);
            }
        });

    check(count == 0);
    check(instrument.adapter->getParamNormalized(shadow) == 0.75);
};

auto tHeldPath = test("Allocations/vst3HeldParameterPathIsOffTheHeap") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto ids = exposedIds(*host.adapter);
    auto& listener = *host.plugin().hostEditListener();
    auto numParams = host.plugin().parameters().size();
    host.handler.edits.reserve(64);

    fillBusyBlock(host, maxBlock, false, 0);
    host.run();

    auto count = allocationsIn(
        [&]
        {
            for (auto i = 0; i < numParams; ++i)
                listener.beginParameterEdit(i);
        });

    fillBusyBlock(host, maxBlock, false, 1);
    count += allocationsIn([&] { host.run(); });

    count += allocationsIn(
        [&]
        {
            for (auto id: ids)
                host.adapter->setParamNormalized(id, 0.5);

            for (auto i = 0; i < numParams; ++i)
                listener.endParameterEdit(i);
        });

    check(count == 0);
    check(host.handler.edits.size() == static_cast<size_t>(numParams) * 2);
};
} // namespace
