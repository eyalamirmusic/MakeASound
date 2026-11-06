// The AAX adapter's audio-thread paths under the allocation ban, driven as Pro
// Tools drives them: whole process-procedure calls with host parameter writes,
// MIDI in and echoed out, over varying block sizes including one past the
// maximum, and the same while every parameter is held. The drivers are
// AAXAllocationDriver.h's, which a consumer's plugin runs through too; what is
// here is the fixtures, the JUCE ports and the checks their members allow.

#include "AAXAllocationDriver.h"

#include <NanoTest/NanoTest.h>

namespace AAXAllocationTests
{
using namespace nano;
using namespace TestPlugins;
using namespace AAXHost;
using namespace AAXAllocation;

namespace
{
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
} // namespace AAXAllocationTests
