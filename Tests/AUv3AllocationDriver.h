#pragma once

// Objective-C++ only. The AUv3 adapter's render block under the allocation ban,
// over any unit of this binary's AUAudioUnit subclass: what
// AUv3AllocationTests.mm measures with its fixtures, and what
// makeasound_add_allocation_tests measures over every class of a consumer's
// describeModule() (PluginAllocations/AUv3.mm). Each driver returns how many
// times the heap was reached for; the checks a fixture's members allow stay in
// the fixture suite.

#include "AllocationProbe.h"
#include "AUv3TestHost.h"

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <string_view>

namespace AUv3Allocation
{
using namespace AUv3Host;
using Probe::allocationsIn;

// Everything a busy host slice carries: an event for every exposed parameter,
// then a note, a controller and a bend at their frames where the plugin takes
// MIDI, built before the ban.
template <typename P>
void busyEvents(UnitHost<P>& host, EventList& events, int block, UInt32 frames)
{
    const auto& list = host.plugin().parameters();
    auto normalized = block % 2 == 0 ? 0.3f : 0.7f;
    auto start = static_cast<AUEventSampleTime>(host.sampleTime);
    auto last = static_cast<AUEventSampleTime>(std::max<UInt32>(frames, 1) - 1);

    events.clear();

    for (auto i = 0; i < list.size(); ++i)
        if (list.isHostExposed(i))
            events.parameter(
                list.entry(i).address, list[i].toPlain(normalized), start);

    if (!host.plugin().getBusLayout().midiInputs.empty())
    {
        events.midi(start, {0x90, 60, 100});
        events.midi(start + last / 3, {0xB0, 1, 64});
        events.midi(start + last * 2 / 3, {0xE0, 0, 80});
        events.midi(start + last, {0x80, 60, 0});
    }
}

// One unmeasured busy slice, then five measured ones of varying sizes, an empty
// one among them, through the internal render block.
template <typename P>
int allocationsOverBlocks(UnitHost<P>& host,
                          UInt32 maxFrames,
                          std::string_view what = {})
{
    auto events = EventList {};
    auto count = 0;
    auto block = 0;
    auto status = AUAudioUnitStatus {noErr};

    busyEvents(host, events, block++, maxFrames);
    status |= host.renderEvents(maxFrames, events.head());

    for (auto size: {64u, 17u, 1u, 63u, 0u})
    {
        auto frames = std::min(size, maxFrames);
        busyEvents(host, events, block++, frames);
        host.sent.clear();

        const auto* head = events.head();
        count += allocationsIn([&] { status |= host.renderEvents(frames, head); });
    }

    nano::check(status == noErr, what);
    return count;
}

// Busy slices at 48 kHz, then re-allocated at 44.1 kHz for slices of 32.
template <typename P>
void checkRenderIsOffTheHeap(UnitHost<P>& host, std::string_view what = {})
{
    nano::check(allocationsOverBlocks(host, maxBlock, what) == 0, what);

    nano::check(host.allocate(44100.0, 32), what);
    nano::check(allocationsOverBlocks(host, 32, what) == 0, what);
}

// The gesture is the editor's, on the message thread, and goes through the
// parameter tree, which is Objective-C; what is measured is the render while the
// gesture holds every parameter and drops the host's events, and after.
template <typename P>
void checkHeldRenderIsOffTheHeap(UnitHost<P>& host, std::string_view what = {})
{
    auto& listener = *host.plugin().hostEditListener();
    auto numParams = host.plugin().parameters().size();

    nano::check(allocationsOverBlocks(host, maxBlock, what) == 0, what);

    for (auto i = 0; i < numParams; ++i)
    {
        listener.beginParameterEdit(i);
        listener.performParameterEdit(i, 0.5f);
    }

    nano::check(allocationsOverBlocks(host, maxBlock, what) == 0, what);

    for (auto i = 0; i < numParams; ++i)
        listener.endParameterEdit(i);

    nano::check(allocationsOverBlocks(host, maxBlock, what) == 0, what);
}

} // namespace AUv3Allocation
