// The AU adapter's audio-thread paths under the allocation ban, driven through
// AudioToolbox's C API as a host drives them: whole AudioUnitRender calls with
// host parameter writes, MIDI in and echoed out, over varying slice sizes and a
// re-initialization, and the same while every parameter is held. The drivers are
// AUAllocationDriver.h's, which a consumer's plugin runs through too; what is
// here is the fixtures, the ports and the checks their members allow.

#include "AUAllocationDriver.h"

#include <MakeASound/Plugin/AU/HostParameters.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>

namespace AUAllocationTests
{
using namespace nano;
using namespace TestPlugins;
using namespace AUHost;
using namespace AUAllocation;

namespace
{
template <typename P>
void checkFixtureRender(FourCC code)
{
    auto host = UnitHost<P>(code);
    checkRenderIsOffTheHeap(host);
}

auto tEffectRender = test("Allocations/auEffectRenderIsOffTheHeap") = []
{
    checkFixtureRender<GainPlugin>("Gain");

    auto host = UnitHost<GainPlugin>("Gain");
    allocationsOverBlocks(host, maxBlock);
    check(host.plugin().processed > 0);
};

auto tInstrumentRender = test("Allocations/auInstrumentRenderIsOffTheHeap") = []
{
    checkFixtureRender<SynthPlugin>("Echo");

    // Packets at one timestamp merge, so the count is read off a full slice.
    auto host = UnitHost<SynthPlugin>("Echo");
    check(busyBlock(host, exposedIds(host), 0, maxBlock, takesMidi(host)) == noErr);
    check(host.plugin().numEvents == 4);
    check(host.sent.size() == 4);
};

template <typename P>
void checkFixtureHeldRender(FourCC code)
{
    auto host = UnitHost<P>(code);
    checkHeldRenderIsOffTheHeap(host);
}

auto tHeldRender = test("Allocations/auHeldParameterRenderIsOffTheHeap") = []
{
    checkFixtureHeldRender<GainPlugin>("Gain");
    checkFixtureHeldRender<SynthPlugin>("Echo");
};

// The plugin moves every parameter itself between renders, so each render writes
// Globals() and raises a moved bit per parameter for the message thread.
auto tPluginSideMoves = test("Allocations/auPluginSideMovesAreOffTheHeap") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
    auto ids = exposedIds(host);

    check(pluginSideMovesAllocations(host) == 0);
    check(host.hostValue(ids[0]) == host.plugin().params.gain.getValue());
};

// A JUCE port under both JUCE schemes: host writes in JUCE's space (the
// normalized value times the maximum) to every listed parameter, the
// non-automatable one included, and the plugin's own moves mirrored back into
// Globals() by the reconcile, over varying slices.
auto tJucePort = test("Allocations/auJucePortRenderIsOffTheHeap") = []
{
    for (auto scheme: {IdScheme::JUCE, IdScheme::JUCELegacyIndex})
    {
        auto host = UnitHost<JucePort::PortPlugin>(portComponentFor(scheme));
        auto space = AU::ParameterSpace(JucePort::description(scheme));
        const auto& list = host.plugin().parameters();
        auto count = 0;
        auto status = OSStatus {noErr};

        auto writeEvery = [&](float normalized)
        {
            for (auto i = 0; i < list.size(); ++i)
                if (space.isListed(list, i))
                    status |= host.hostWrites(
                        list.entry(i).hostId,
                        space.toHost(list.entry(i), list[i].toPlain(normalized)));
        };

        writeEvery(0.5f);
        status |= host.render(maxBlock);

        auto block = 0;

        for (auto size: {64u, 17u, 1u, 63u, 0u})
        {
            auto normalized = block++ % 2 == 0 ? 0.3f : 0.7f;
            count += allocationsIn(
                [&]
                {
                    writeEvery(normalized);
                    status |= host.render(size);
                });

            for (auto i = 0; i < list.size(); ++i)
                list[i].setNormalized(1.f - normalized);

            count += allocationsIn([&] { status |= host.render(size); });
        }

        check(status == noErr);
        check(count == 0);

        // The plugin's last move, mirrored by the next block that renders: one at
        // a later time, since the SDK renders a timestamp once and the empty
        // slices did not move it.
        host.sampleTime += maxBlock;
        check(host.render(maxBlock) == noErr);
        const auto& cutoff = host.plugin().params.cutoff;
        check(host.hostValue(list.entry(list.indexOf(&cutoff)).hostId)
              == space.toHost(list.entry(list.indexOf(&cutoff)), cutoff.getValue()));
    }
};

// A Tamber Plug port (IdScheme::TamberPlug) with a post-port parameter: host
// writes in its plain space to Plug's index ids and the FNV one, the bypass
// property, and the plugin's own moves mirrored back, over varying slices.
auto tPlugPort = test("Allocations/auTamberPlugPortRenderIsOffTheHeap") = []
{
    const auto& description =
        *TamberPlugFixtures::plugDescriptionFor(FourCC("Psv2").toUint32());
    auto host =
        UnitHost<TamberPlugFixtures::NewerSVFPort>(plugPortComponentFor("Psv2"));
    auto space = AU::ParameterSpace(description);
    const auto& list = host.plugin().parameters();
    auto count = 0;
    auto status = OSStatus {noErr};

    auto writeEvery = [&](float normalized)
    {
        for (auto i = 0; i < list.size(); ++i)
            if (space.isListed(list, i))
                status |= host.hostWrites(
                    list.entry(i).hostId,
                    space.toHost(list.entry(i), list[i].toPlain(normalized)));
    };

    writeEvery(0.5f);
    status |= host.render(maxBlock);

    auto block = 0;

    for (auto size: {64u, 17u, 1u, 63u, 0u})
    {
        auto normalized = block++ % 2 == 0 ? 0.3f : 0.7f;
        count += allocationsIn(
            [&]
            {
                writeEvery(normalized);
                status |= host.render(size);
            });

        for (auto i = 0; i < list.size(); ++i)
            list[i].setNormalized(1.f - normalized);

        count += allocationsIn([&] { status |= host.render(size); });
    }

    check(status == noErr);
    check(count == 0);
};
} // namespace
} // namespace AUAllocationTests
