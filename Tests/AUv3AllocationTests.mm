// The AUv3 adapter's render block under the allocation ban, driven as the base
// class drives it: the internal render block with a list of parameter and MIDI
// events, echoed MIDI leaving through the host's block, over varying slice sizes
// and a re-allocation, and the same while every parameter is held. The drivers
// are AUv3AllocationDriver.h's, which a consumer's plugin runs through too; what
// is here is the fixtures, the port and the checks their members allow.

#include "AUv3AllocationDriver.h"

#include <MakeASound/Plugin/AU/HostParameters.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <cmath>

using namespace nano;
using namespace TestPlugins;
using namespace AUv3Host;
using namespace AUv3Allocation;

namespace
{
bool near(float a, float b)
{
    return std::abs(a - b) < 1e-3f;
}

template <typename P>
void checkFixtureRender(FourCC code)
{
    auto host = UnitHost<P>(code);
    checkRenderIsOffTheHeap(host);
}

auto tEffectRender = test("Allocations/auv3EffectRenderIsOffTheHeap") = []
{
    checkFixtureRender<GainPlugin>("Gain");

    auto host = UnitHost<GainPlugin>("Gain");
    allocationsOverBlocks(host, maxBlock);
    check(host.plugin().processed > 0);
};

auto tInstrumentRender = test("Allocations/auv3InstrumentRenderIsOffTheHeap") = []
{
    checkFixtureRender<SynthPlugin>("Echo");

    auto host = UnitHost<SynthPlugin>("Echo");
    auto events = EventList {};
    busyEvents(host, events, 0, maxBlock);
    check(host.renderEvents(maxBlock, events.head()) == noErr);
    check(host.plugin().numEvents == 4);
    check(host.sent.size() == 4);
};

template <typename P>
void checkFixtureHeldRender(FourCC code)
{
    auto host = UnitHost<P>(code);
    checkHeldRenderIsOffTheHeap(host);
}

auto tHeldRender = test("Allocations/auv3HeldParameterRenderIsOffTheHeap") = []
{
    checkFixtureHeldRender<GainPlugin>("Gain");
    checkFixtureHeldRender<SynthPlugin>("Echo");
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
