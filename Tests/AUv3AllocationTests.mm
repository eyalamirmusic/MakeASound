// The AUv3 adapter's render block under the allocation ban, driven as the base
// class drives it: the internal render block with a list of parameter and MIDI
// events, echoed MIDI leaving through the host's block, over varying slice sizes
// and a re-allocation, and the same while every parameter is held.

#include "AllocationProbe.h"
#include "AUv3TestHost.h"

#include <MakeASound/Plugin/AU/HostParameters.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <cmath>

using namespace nano;
using namespace TestPlugins;
using namespace AUv3Host;
using Probe::allocationsIn;

namespace
{
bool near(float a, float b)
{
    return std::abs(a - b) < 1e-3f;
}

// Everything a busy host slice carries: an event for every exposed parameter,
// then a note, a controller and a bend at their frames, built before the ban.
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

template <typename P>
int allocationsOverBlocks(UnitHost<P>& host, UInt32 maxFrames)
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

    check(status == noErr);
    return count;
}

template <typename P>
void checkRenderIsOffTheHeap(FourCC code)
{
    auto host = UnitHost<P>(code);

    check(allocationsOverBlocks(host, maxBlock) == 0);

    check(host.allocate(44100.0, 32));
    check(allocationsOverBlocks(host, 32) == 0);
}

auto tEffectRender = test("Allocations/auv3EffectRenderIsOffTheHeap") = []
{
    checkRenderIsOffTheHeap<GainPlugin>("Gain");

    auto host = UnitHost<GainPlugin>("Gain");
    allocationsOverBlocks(host, maxBlock);
    check(host.plugin().processed > 0);
};

auto tInstrumentRender = test("Allocations/auv3InstrumentRenderIsOffTheHeap") = []
{
    checkRenderIsOffTheHeap<SynthPlugin>("Echo");

    auto host = UnitHost<SynthPlugin>("Echo");
    auto events = EventList {};
    busyEvents(host, events, 0, maxBlock);
    check(host.renderEvents(maxBlock, events.head()) == noErr);
    check(host.plugin().numEvents == 4);
    check(host.sent.size() == 4);
};

// The gesture is the editor's, on the message thread, and goes through the
// parameter tree, which is Objective-C; what is measured is the render while the
// gesture holds every parameter and drops the host's events.
template <typename P>
void checkHeldRenderIsOffTheHeap(FourCC code)
{
    auto host = UnitHost<P>(code);
    auto& listener = *host.plugin().hostEditListener();
    auto numParams = host.plugin().parameters().size();

    check(allocationsOverBlocks(host, maxBlock) == 0);

    for (auto i = 0; i < numParams; ++i)
    {
        listener.beginParameterEdit(i);
        listener.performParameterEdit(i, 0.5f);
    }

    check(allocationsOverBlocks(host, maxBlock) == 0);

    for (auto i = 0; i < numParams; ++i)
        listener.endParameterEdit(i);

    check(allocationsOverBlocks(host, maxBlock) == 0);
}

auto tHeldRender = test("Allocations/auv3HeldParameterRenderIsOffTheHeap") = []
{
    checkHeldRenderIsOffTheHeap<GainPlugin>("Gain");
    checkHeldRenderIsOffTheHeap<SynthPlugin>("Echo");
};

// A JUCE port under both JUCE schemes: an event for every listed parameter at
// its Entry::address (64-bit hashCode64 under JUCE, the index under legacy ids)
// in JUCE's space, each found through the sorted address table, over varying
// slices.
auto tJucePort = test("Allocations/auv3JucePortEventsAreOffTheHeap") = []
{
    for (auto scheme: {IdScheme::JUCE, IdScheme::JUCELegacyIndex})
    {
        const auto& description = JucePort::description(scheme);
        auto host = UnitHost<JucePort::PortPlugin>(description);
        auto space = AU::ParameterSpace(description);
        const auto& list = host.plugin().parameters();
        auto events = EventList {};
        auto count = 0;
        auto status = AUAudioUnitStatus {noErr};
        auto block = 0;

        auto fill = [&](float normalized)
        {
            auto start = static_cast<AUEventSampleTime>(host.sampleTime);
            events.clear();

            for (auto i = 0; i < list.size(); ++i)
                if (space.isListed(list, i))
                    events.parameter(
                        list.entry(i).address,
                        space.toHost(list.entry(i), list[i].toPlain(normalized)),
                        start);
        };

        fill(0.5f);
        status |= host.renderEvents(maxBlock, events.head());

        for (auto size: {64u, 17u, 1u, 63u, 0u})
        {
            auto normalized = block++ % 2 == 0 ? 0.3f : 0.7f;
            fill(normalized);

            const auto* head = events.head();
            count +=
                allocationsIn([&] { status |= host.renderEvents(size, head); });

            check(near(host.plugin().params.cutoff.getNormalized(), normalized));
        }

        check(status == noErr);
        check(count == 0);
    }
};
} // namespace
