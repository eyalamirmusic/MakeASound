// A hosted Audio Unit's audio-thread paths under the allocation ban: render with
// the pull input, the host callbacks and the fallback playhead, values scheduled
// from the audio thread, and MIDI in. The unit is the test module's own, so what
// is measured is the host side plus a plugin known to be clean.

#include "AllocationProbe.h"
#include "AUTestHost.h"

#include <MakeASound/Hosting/AU/HostedAudioUnit.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;
using Probe::allocationsIn;

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

ProcessContext contextFor(HostedPlugin& plugin)
{
    auto context = ProcessContext();
    const auto layout = plugin.getBusLayout();
    context.prepare(layout);

    for (auto i = 0; i < layout.inputs.size(); ++i)
        context.inputs[i].setSize(layout.inputs[i].numChannels, blockSize);

    for (auto i = 0; i < layout.outputs.size(); ++i)
        context.outputs[i].setSize(layout.outputs[i].numChannels, blockSize);

    plugin.prepare(
        {.sampleRate = 48000, .maxBlockSize = blockSize, .layout = layout});
    return context;
}

auto tEffect = test("Allocations/aHostedAudioUnitsBlockTouchesNothing") = []
{
    auto unit = loadTestUnit("Gain");

    if (unit == nullptr)
        return;

    auto context = contextFor(*unit);
    auto playing = Playhead();
    playing.isPlaying = true;
    unit->setFallbackPlayhead(playing);

    for (auto i = 0; i < 4; ++i)
        unit->process(context);

    auto count = allocationsIn(
        [&]
        {
            for (auto block = 0; block < 32; ++block)
            {
                unit->setParameterFromAudioThread(0, (block % 8) / 8.f);
                unit->process(context);
            }
        });

    check(count == 0);
};

auto tInstrument = test("Allocations/aHostedAudioUnitsMidiTouchesNothing") = []
{
    auto unit = loadTestUnit("Echo");

    if (unit == nullptr)
        return;

    auto context = contextFor(*unit);

    for (auto i = 0; i < 4; ++i)
        unit->process(context);

    auto count = allocationsIn(
        [&]
        {
            for (auto block = 0; block < 32; ++block)
            {
                auto pitch = 60 + block % 12;
                context.clearMidi();
                context.midiIn[0].add(MIDI::Event::noteOn(0, pitch, 0.7f, 3));
                context.midiIn[0].add(MIDI::Event::pitchBend(0, 0.25f, 4));
                context.midiIn[0].add(MIDI::Event::noteOff(0, pitch, 0.f, 9));
                unit->process(context);
            }
        });

    check(count == 0);
};
} // namespace
