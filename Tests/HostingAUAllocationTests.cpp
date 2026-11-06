// A hosted Audio Unit's audio-thread paths under the allocation ban: render with
// the pull input, the host callbacks and the fallback playhead, values scheduled
// from the audio thread, and MIDI in. The unit is the test module's own, so what
// is measured is the host side plus a plugin known to be clean. The drivers are
// HostingAllocationDriver.h's, which a consumer's plugin runs through too.

#include "HostingAllocationDriver.h"
#include "AUTestHost.h"
#include "MisbehavingAU.h"

#include <MakeASound/Hosting/AU/HostedAudioUnit.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <cstdint>

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

    // Every kind of event the host encodes for the unit, SysEx included.
    constexpr auto sysEx =
        std::array<std::uint8_t, 6> {0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7};

    auto count = allocationsOverBlocks(
        *unit,
        context,
        [&](int block)
        {
            auto pitch = 60 + block % 12;
            auto bend = static_cast<float>(block % 9) / 4.f - 1.f;
            context.clearMidi();
            context.midiIn[0].add(MIDI::Event::noteOn(0, pitch, 0.7f, 3));
            context.midiIn[0].add(MIDI::Event::pitchBend(0, bend, 4));
            context.midiIn[0].add(MIDI::Event::controlChange(0, 1, 0.5f, 5));
            context.midiIn[0].add(MIDI::Event::channelAftertouch(0, 0.3f, 6));
            context.midiIn[0].add(MIDI::Event::polyAftertouch(0, pitch, 0.4f, 7));
            context.midiIn[0].add(MIDI::Event::programChange(0, 5, 8));
            context.midiIn[0].add(
                MIDI::Event::sysEx(sysEx.data(), static_cast<int>(sysEx.size())));
            context.midiIn[0].add(MIDI::Event::noteOff(0, pitch, 0.f, 9));
        });

    check(count == 0);

    // The unit's MIDI out came back through the callback, inside the ban.
    check(context.midiOut.size() == 1 && context.midiOut[0].size() == 8);
};

// A unit that pulls its input with no memory of its own, and tells its latency
// from inside every Render: the host's own input memory, and the listener that
// only marks what was told for the main run loop.
auto tPullAndTell = test(
    "Allocations/aHostedAudioUnitPullingAndTellingFromRenderTouchesNothing") = []
{
    auto behaviour = MisbehavingAU::Behaviour();
    behaviour.pullIntoHostMemory = true;
    behaviour.latencyOnFirstRender = 0.01;
    behaviour.tellLatencyOnEveryRender = true;
    auto loaded = MisbehavingAU::load(behaviour);

    if (!loaded)
        return;

    auto& unit = *loaded.plugin;
    auto context = contextFor(unit, blockSize);
    warmUp(unit, context);

    auto count = allocationsOverBlocks(unit, context, [](int) {});
    check(count == 0);

    const auto& record = loaded.unit->record;
    check(record.pulls > 0 && record.nonSilentSamples == 0);
    check(record.reentrantReads == 0);
};
} // namespace
} // namespace HostingAUAllocationTests
