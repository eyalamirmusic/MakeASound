// A hosted Audio Unit's audio-thread paths under the allocation ban: render with
// the pull input, the host callbacks and the fallback playhead, values scheduled
// from the audio thread, and MIDI in. The unit is the test module's own, so what
// is measured is the host side plus a plugin known to be clean. The drivers are
// HostingAllocationDriver.h's, which a consumer's plugin runs through too.

#include "HostingAllocationDriver.h"
#include "AUTestHost.h"

#include <MakeASound/Hosting/AU/HostedAudioUnit.h>

#include <NanoTest/NanoTest.h>

namespace HostingAUAllocationTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;
using namespace HostingAllocation;

namespace
{
constexpr auto blockSize = TestPlugins::blockSize;

OwningPointer<HostedPlugin> loadTestUnit(FourCC code)
{
    auto component = AUHost::registerComponent(AUHost::descriptionFor(code));
    auto loaded = loadAudioUnit(component, {}, {});
    check(loaded.plugin != nullptr);
    return std::move(loaded.plugin);
}

auto tEffect = test("Allocations/aHostedAudioUnitsBlockTouchesNothing") = []
{
    auto unit = loadTestUnit("Gain");

    if (unit == nullptr)
        return;

    auto context = contextFor(*unit, blockSize);
    auto playing = Playhead();
    playing.isPlaying = true;
    unit->setFallbackPlayhead(playing);

    warmUp(*unit, context);

    auto count = allocationsOverBlocks(
        *unit,
        context,
        [&](int block) { unit->setParameterFromAudioThread(0, (block % 8) / 8.f); });

    check(count == 0);
};

auto tInstrument = test("Allocations/aHostedAudioUnitsMidiTouchesNothing") = []
{
    auto unit = loadTestUnit("Echo");

    if (unit == nullptr)
        return;

    auto context = contextFor(*unit, blockSize);
    warmUp(*unit, context);

    auto count = allocationsOverBlocks(
        *unit,
        context,
        [&](int block)
        {
            auto pitch = 60 + block % 12;
            context.clearMidi();
            context.midiIn[0].add(MIDI::Event::noteOn(0, pitch, 0.7f, 3));
            context.midiIn[0].add(MIDI::Event::pitchBend(0, 0.25f, 4));
            context.midiIn[0].add(MIDI::Event::noteOff(0, pitch, 0.f, 9));
        });

    check(count == 0);

    // The unit's MIDI out came back through the callback, inside the ban.
    check(context.midiOut.size() == 1 && context.midiOut[0].size() == 3);
};
} // namespace
} // namespace HostingAUAllocationTests
