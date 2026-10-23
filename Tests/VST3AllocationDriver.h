#pragma once

// The VST3 adapter's audio-thread paths under the allocation ban, over any
// plugin: what VST3AllocationTests.cpp measures with its fixtures, and what
// makeasound_add_allocation_tests measures over every class of a consumer's
// describeModule() (PluginAllocations/VST3.cpp). Each driver returns how many
// times the heap was reached for; the checks a fixture's members allow stay in
// the fixture suite.

#include "AllocationProbe.h"
#include "VST3TestHost.h"

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <string_view>
#include <vector>

namespace VST3Allocation
{
using namespace VST3Host;
using Probe::allocationsIn;

inline std::vector<Vst::ParamID> exposedIds(MakeASound::VST3::Adapter& adapter)
{
    auto ids = std::vector<Vst::ParamID> {};
    const auto& list = adapter.plugin().parameters();
    auto everyOne = list.identity().registration == MakeASound::Registration::All;

    // A JUCE scheme registers the non-automatable ones too.
    for (auto i = 0; i < list.size(); ++i)
        if (everyOne || list.isHostExposed(i))
            ids.push_back(list.entry(i).hostId);

    return ids;
}

inline Vst::Event note(bool on, int offset)
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

// Everything a busy host block carries, filled outside the measurement:
// automation of every exposed parameter, the mod wheel and bend through the
// MIDI mapping where there is one, a note on and off, and a playing transport.
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

// One unmeasured busy block, then five measured ones of varying sizes, an empty
// one among them.
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

// Busy blocks at 48 kHz, then reconfigured to 44.1 kHz in blocks of 32, then a
// bounce: the offline flag on every block.
template <typename P>
void checkProcessIsOffTheHeap(AdapterHost<P>& host,
                              bool inPlace,
                              std::string_view what = {})
{
    nano::check(allocationsOverBlocks(host, inPlace, maxBlock) == 0, what);

    host.configure(44100.0, 32);
    nano::check(allocationsOverBlocks(host, inPlace, 32) == 0, what);

    host.configure(44100.0, 32, Vst::kOffline);
    nano::check(allocationsOverBlocks(host, inPlace, 32) == 0, what);
}

// The controller's setParamNormalized on every exposed parameter, and on the
// mod wheel's shadow parameter where the plugin maps MIDI controllers.
template <typename P>
int setParamNormalizedAllocations(AdapterHost<P>& host)
{
    auto ids = exposedIds(*host.adapter);
    auto modWheel = Vst::ParamID {};

    if (host.adapter->getMidiControllerAssignment(0, 0, 1, modWheel)
        == Steinberg::kResultTrue)
        ids.push_back(modWheel);

    return allocationsIn(
        [&]
        {
            for (auto value: {0.25, 0.75})
                for (auto id: ids)
                    host.adapter->setParamNormalized(id, value);
        });
}

// The plugin moves every parameter itself between blocks (a preset of its own),
// and each block leaves a point per parameter in the output changes.
template <typename P>
int pluginSideChangesAllocations(AdapterHost<P>& host, std::string_view what = {})
{
    auto ids = exposedIds(*host.adapter);
    const auto& list = host.plugin().parameters();
    auto& wrapper = host.adapter->wrapper();
    auto count = 0;
    auto block = 0;

    auto moveEveryParameter = [&]
    {
        host.clearQueues();

        for (auto id: ids)
            wrapper.setNormalizedParameter(list.indexOfHostId(id),
                                           block % 2 == 0 ? 0.1f : 0.9f);
    };

    moveEveryParameter();
    host.fillBlock(maxBlock);
    host.run();

    for (auto size: {64, 17, 1, 0, 63})
    {
        ++block;
        moveEveryParameter();
        host.fillBlock(std::min(size, maxBlock));
        count += allocationsIn([&] { host.run(); });
        nano::check(host.outputChanges.getParameterCount()
                        == static_cast<Steinberg::int32>(ids.size()),
                    what);
    }

    return count;
}

// The held path an editor gesture takes: every parameter begun, a busy block
// while they are held, then a host write to each and every gesture ended, with
// room in the handler for a begin and an end per parameter.
template <typename P>
int heldPathAllocations(AdapterHost<P>& host)
{
    auto ids = exposedIds(*host.adapter);
    auto& listener = *host.plugin().hostEditListener();
    auto numParams = host.plugin().parameters().size();
    host.handler.edits.clear();
    host.handler.edits.reserve(std::max<size_t>(64, size_t(numParams) * 2));

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

    return count;
}

} // namespace VST3Allocation
