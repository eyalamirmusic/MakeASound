// The AU adapter's audio-thread paths under the allocation ban, driven through
// AudioToolbox's C API as a host drives them: whole AudioUnitRender calls with
// host parameter writes, MIDI in and echoed out, over varying slice sizes and a
// re-initialization, and the same while every parameter is held.

#include "AllocationProbe.h"
#include "AUTestHost.h"

#include <MakeASound/Plugin/AU/HostParameters.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>

namespace AUAllocationTests
{
using namespace nano;
using namespace TestPlugins;
using namespace AUHost;
using Probe::allocationsIn;

namespace
{
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

// Everything a busy host slice carries: a write to every parameter, notes, a
// controller and a bend at their frames, then the render.
template <typename P>
OSStatus busyBlock(UnitHost<P>& host,
                   const std::vector<AudioUnitParameterID>& ids,
                   int block,
                   UInt32 frames)
{
    const auto& list = host.plugin().parameters();
    auto normalized = block % 2 == 0 ? 0.3f : 0.7f;

    // In the host's space: 0..1 for a curve no display flag draws (the skewed
    // cutoff, which the reconcile then denormalizes on the render thread).
    for (auto id: ids)
    {
        const auto& param = list[list.indexOfHostId(id)];
        host.hostWrites(id,
                        param.curve() == Parameter::Curve::Other
                            ? normalized
                            : param.toPlain(normalized));
    }

    if (host.hasMidiOut())
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

template <typename P>
int allocationsOverBlocks(UnitHost<P>& host, UInt32 maxFrames)
{
    auto ids = exposedIds(host);
    auto count = 0;
    auto block = 0;
    auto status = OSStatus {noErr};

    status |= busyBlock(host, ids, block++, maxFrames);

    for (auto size: {64u, 17u, 1u, 63u, 0u})
    {
        auto frames = std::min(size, maxFrames);
        count +=
            allocationsIn([&] { status |= busyBlock(host, ids, block++, frames); });
    }

    check(status == noErr);
    return count;
}

template <typename P>
void checkRenderIsOffTheHeap(FourCC code)
{
    auto host = UnitHost<P>(code);

    check(allocationsOverBlocks(host, maxBlock) == 0);

    check(host.initialize(44100.0, 32) == noErr);
    check(allocationsOverBlocks(host, 32) == 0);
}

auto tEffectRender = test("Allocations/auEffectRenderIsOffTheHeap") = []
{
    checkRenderIsOffTheHeap<GainPlugin>("Gain");

    auto host = UnitHost<GainPlugin>("Gain");
    allocationsOverBlocks(host, maxBlock);
    check(host.plugin().processed > 0);
};

auto tInstrumentRender = test("Allocations/auInstrumentRenderIsOffTheHeap") = []
{
    checkRenderIsOffTheHeap<SynthPlugin>("Echo");

    // Packets at one timestamp merge, so the count is read off a full slice.
    auto host = UnitHost<SynthPlugin>("Echo");
    check(busyBlock(host, exposedIds(host), 0, maxBlock) == noErr);
    check(host.plugin().numEvents == 4);
    check(host.sent.size() == 4);
};

template <typename P>
void checkHeldRenderIsOffTheHeap(FourCC code)
{
    auto host = UnitHost<P>(code);
    auto& listener = *host.plugin().hostEditListener();
    auto numParams = host.plugin().parameters().size();

    check(allocationsOverBlocks(host, maxBlock) == 0);

    // The gesture is the editor's, on the message thread, but it is the path a
    // drag takes many times a second.
    check(allocationsIn(
              [&]
              {
                  for (auto i = 0; i < numParams; ++i)
                  {
                      listener.beginParameterEdit(i);
                      listener.performParameterEdit(i, 0.5f);
                  }
              })
          == 0);

    check(allocationsOverBlocks(host, maxBlock) == 0);

    check(allocationsIn(
              [&]
              {
                  for (auto i = 0; i < numParams; ++i)
                      listener.endParameterEdit(i);
              })
          == 0);

    check(allocationsOverBlocks(host, maxBlock) == 0);
}

auto tHeldRender = test("Allocations/auHeldParameterRenderIsOffTheHeap") = []
{
    checkHeldRenderIsOffTheHeap<GainPlugin>("Gain");
    checkHeldRenderIsOffTheHeap<SynthPlugin>("Echo");
};

// The plugin moves every parameter itself between renders, so each render writes
// Globals() and raises a moved bit per parameter for the message thread.
auto tPluginSideMoves = test("Allocations/auPluginSideMovesAreOffTheHeap") = []
{
    auto host = UnitHost<GainPlugin>("Gain");
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

    check(status == noErr);
    check(count == 0);
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
