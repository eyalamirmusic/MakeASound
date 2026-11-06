#pragma once

// The AU adapter's audio-thread paths under the allocation ban, over any unit of
// this binary's module: what AUAllocationTests.cpp measures with its fixtures,
// and what makeasound_add_allocation_tests measures over every class of a
// consumer's describeModule() (PluginAllocations/AU.cpp). Each driver returns
// how many times the heap was reached for; the checks a fixture's members allow
// stay in the fixture suite.

#include "AllocationProbe.h"
#include "AUTestHost.h"

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <string_view>
#include <vector>

namespace AUAllocation
{
using namespace AUHost;
using Probe::allocationsIn;

template <typename P>
std::vector<AudioUnitParameterID> exposedIds(UnitHost<P>& host)
{
    auto ids = std::vector<AudioUnitParameterID> {};
    const auto& list = host.plugin().parameters();

    for (auto i = 0; i < list.size(); ++i)
        if (list.isHostExposed(i))
            ids.push_back(list.entry(i).hostId);

    return ids;
}

// Whether the plugin declares a MIDI input; asked outside the ban, since the
// layout is a value with vectors in it.
template <typename P>
bool takesMidi(UnitHost<P>& host)
{
    return !host.plugin().getBusLayout().midiInputs.empty();
}

// Everything a busy host slice carries: a write to every parameter, notes, a
// controller and a bend at their frames when `midi` (the plugin takes MIDI),
// then the render.
template <typename P>
OSStatus busyBlock(UnitHost<P>& host,
                   const std::vector<AudioUnitParameterID>& ids,
                   int block,
                   UInt32 frames,
                   bool midi)
{
    const auto& list = host.plugin().parameters();
    auto normalized = block % 2 == 0 ? 0.3f : 0.7f;

    // In the host's space: 0..1 for a curve no display flag draws (the skewed
    // cutoff, which the reconcile then denormalizes on the render thread).
    for (auto id: ids)
    {
        const auto& param = list[list.indexOfHostId(id)];
        host.hostWrites(id,
                        param.curve() == MakeASound::Parameter::Curve::Other
                            ? normalized
                            : param.toPlain(normalized));
    }

    if (midi)
    {
        auto last = std::max<UInt32>(frames, 1) - 1;
        MusicDeviceMIDIEvent(host.unit, 0x90, 60, 100, 0);
        MusicDeviceMIDIEvent(host.unit, 0xB0, 1, 64, last / 3);
        MusicDeviceMIDIEvent(host.unit, 0xE0, 0, 80, last * 2 / 3);
        MusicDeviceMIDIEvent(host.unit, 0x80, 60, 0, last);
    }

    host.sent.clear();
    return host.render(frames);
}

// One unmeasured busy slice, then five measured ones of varying sizes, an empty
// one among them; the host's writes and MIDI are inside the measurement.
template <typename P>
int allocationsOverBlocks(UnitHost<P>& host,
                          UInt32 maxFrames,
                          std::string_view what = {})
{
    auto ids = exposedIds(host);
    auto midi = takesMidi(host);
    auto count = 0;
    auto block = 0;
    auto status = OSStatus {noErr};

    status |= busyBlock(host, ids, block++, maxFrames, midi);

    for (auto size: {64u, 17u, 1u, 63u, 0u})
    {
        auto frames = std::min(size, maxFrames);
        count += allocationsIn(
            [&] { status |= busyBlock(host, ids, block++, frames, midi); });
    }

    nano::check(status == noErr, what);
    return count;
}

// Busy slices at 48 kHz, then re-initialized at 44.1 kHz in slices of 32.
template <typename P>
void checkRenderIsOffTheHeap(UnitHost<P>& host, std::string_view what = {})
{
    nano::check(allocationsOverBlocks(host, maxBlock, what) == 0, what);

    nano::check(host.initialize(44100.0, 32) == noErr, what);
    nano::check(allocationsOverBlocks(host, 32, what) == 0, what);
}

// The same while every parameter is held by an editor gesture, and after the
// gestures end. The gesture is the editor's, on the message thread, but it is
// the path a drag takes many times a second, so it is measured too.
template <typename P>
void checkHeldRenderIsOffTheHeap(UnitHost<P>& host, std::string_view what = {})
{
    auto& listener = *host.plugin().hostEditListener();
    auto numParams = host.plugin().parameters().size();

    nano::check(allocationsOverBlocks(host, maxBlock, what) == 0, what);

    nano::check(allocationsIn(
                    [&]
                    {
                        for (auto i = 0; i < numParams; ++i)
                        {
                            listener.beginParameterEdit(i);
                            listener.performParameterEdit(i, 0.5f);
                        }
                    })
                    == 0,
                what);

    nano::check(allocationsOverBlocks(host, maxBlock, what) == 0, what);

    nano::check(allocationsIn(
                    [&]
                    {
                        for (auto i = 0; i < numParams; ++i)
                            listener.endParameterEdit(i);
                    })
                    == 0,
                what);

    nano::check(allocationsOverBlocks(host, maxBlock, what) == 0, what);
}

// The plugin moves every parameter itself between renders, so each render writes
// Globals() and raises a moved bit per parameter for the message thread.
template <typename P>
int pluginSideMovesAllocations(UnitHost<P>& host, std::string_view what = {})
{
    auto ids = exposedIds(host);
    const auto& list = host.plugin().parameters();
    auto count = 0;
    auto status = OSStatus {noErr};

    for (auto block = 0; block < 6; ++block)
    {
        for (auto id: ids)
            list[list.indexOfHostId(id)].setNormalized(block % 2 == 0 ? 0.1f : 0.9f);

        count += allocationsIn([&] { status |= host.render(maxBlock); });
    }

    nano::check(status == noErr, what);
    return count;
}

} // namespace AUAllocation
