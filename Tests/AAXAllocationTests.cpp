// The AAX adapter's audio-thread paths under the allocation ban, driven as Pro
// Tools drives them: whole process-procedure calls with host parameter writes,
// MIDI in and echoed out, over varying block sizes including one past the
// maximum, and the same while every parameter is held.

#include "AllocationProbe.h"
#include "AAXTestHost.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace TestPlugins;
using namespace AAXHost;
using Probe::allocationsIn;

namespace
{
// Everything a busy block carries: a write to every exposed parameter, notes, a
// controller, a bend and a split SysEx at their timestamps, then the render.
template <typename P>
void busyBlock(AdapterHost<P>& host, int block, int frames)
{
    const auto& list = host.plugin().parameters();
    auto normalized = block % 2 == 0 ? 0.3 : 0.7;

    for (auto i = 0; i < list.size(); ++i)
        if (list.isHostExposed(i))
            host.hostWrites(list[i], normalized);

    host.midiIn.clear();
    host.midiOut.clear();

    auto last = static_cast<uint32_t>(std::max(frames, 1) - 1);
    host.midiIn.add(0, {0x90, 60, 100});
    host.midiIn.add(last / 3, {0xb0, 1, 64});
    host.midiIn.add(last / 2, {0xf0, 0x7e, 0x7f, 0x09});
    host.midiIn.add(last / 2, {0x01, 0xf7});
    host.midiIn.add(last * 2 / 3, {0xe0, 0, 80});
    host.midiIn.add(last, {0x80, 60, 0});

    host.render(frames);
}

template <typename P>
int allocationsOverBlocks(AdapterHost<P>& host)
{
    auto count = 0;
    auto block = 0;

    busyBlock(host, block++, blockSize);

    for (auto size: {64, 17, 1, 63, 1024, 2500, 0})
        count += allocationsIn([&] { busyBlock(host, block++, size); });

    return count;
}

template <typename P>
void checkHeldRenderIsOffTheHeap(AdapterHost<P>& host)
{
    auto& listener = *host.plugin().hostEditListener();
    auto numParams = host.plugin().parameters().size();

    check(allocationsOverBlocks(host) == 0);

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

    check(allocationsOverBlocks(host) == 0);

    check(allocationsIn(
              [&]
              {
                  for (auto i = 0; i < numParams; ++i)
                      listener.endParameterEdit(i);
              })
          == 0);

    check(allocationsOverBlocks(host) == 0);
}

auto tEffect = test("Allocations/aaxEffectRenderIsOffTheHeap") = []
{
    for (auto inPlace: {false, true})
    {
        auto host = AdapterHost<GainPlugin>("Gain");

        if (inPlace)
            host.processInPlace();

        check(allocationsOverBlocks(host) == 0);
        check(host.plugin().processed > 0);
    }
};

auto tInstrument = test("Allocations/aaxInstrumentRenderIsOffTheHeap") = []
{
    auto host = AdapterHost<SynthPlugin>(
        "Echo", AAX_eStemFormat_None, AAX_eStemFormat_Stereo);
    check(allocationsOverBlocks(host) == 0);

    busyBlock(host, 0, blockSize);
    check(host.plugin().numEvents == 5);
    check(host.midiOut.numPosted == 4);
};

// A JUCE port: every host write found by JUCE's AAX id (the bypass by
// "MasterBypassID", the legacy scheme's by index) with no allocation of ours.
// The SDK's own GetParameterByID builds a std::string from the id, so an id past
// the library's short-string capacity (22 bytes here) allocates there; these
// stay inside it.
auto tJuce = test("Allocations/aaxJuceSchemeRenderIsOffTheHeap") = []
{
    for (auto* effect: {"com.juce.tests", "com.juce.tests.legacy"})
    {
        auto host = AdapterHost<JuceFixtures::FixturePlugin>(JuceEffect {effect});
        check(allocationsOverBlocks(host) == 0);
        checkHeldRenderIsOffTheHeap(host);
    }

    auto host = AdapterHost<GainPlugin>(JuceEffect {"com.juce.tests.gain"});
    check(allocationsOverBlocks(host) == 0);
    check(host.plugin().processed > 0);
};

auto tHeld = test("Allocations/aaxHeldParameterRenderIsOffTheHeap") = []
{
    {
        auto host = AdapterHost<GainPlugin>("Gain");
        checkHeldRenderIsOffTheHeap(host);
    }

    auto host = AdapterHost<SynthPlugin>(
        "Echo", AAX_eStemFormat_None, AAX_eStemFormat_Stereo);
    checkHeldRenderIsOffTheHeap(host);
};
} // namespace
