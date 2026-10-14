// A hosted plugin's audio-thread paths under the allocation ban: process() with
// output parameters and the fallback playhead, setParameterFromAudioThread, edits
// queued from the message thread, and MIDI in and out. The hosted plugins are the
// fixtures, so what is measured is the host side plus a plugin known to be clean.

#include "AllocationProbe.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <string>

using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;
using Probe::allocationsIn;

namespace
{
constexpr auto blockSize = 128;

struct DefaultFormats : FormatManager
{
    DefaultFormats() { addDefaultFormats(); }
};

OwningPointer<HostedPlugin> loadFixture(const FormatManager& formats,
                                        const char* name)
{
    const auto path = (std::filesystem::path(MAKEASOUND_HOST_FIXTURE_VST3_DIR)
                       / (std::string(name) + ".vst3"))
                          .string();

    auto scan = formats.scanTarget(path).scan;
    check(scan.classes.size() == 1);

    if (scan.classes.empty())
        return {};

    return std::move(formats.load(scan.classes[0]).plugin);
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

auto tEffect = test("Allocations/aHostedEffectsBlockTouchesNothing") = []
{
    const auto formats = DefaultFormats();
    auto plugin = loadFixture(formats, "HostFixtureEffect");
    check(plugin != nullptr);

    if (plugin == nullptr)
        return;

    auto context = contextFor(*plugin);
    auto playing = Playhead();
    playing.isPlaying = true;
    plugin->setFallbackPlayhead(playing);

    // Warm up: the plugin's first blocks may settle anything lazy of its own.
    for (auto i = 0; i < 4; ++i)
        plugin->process(context);

    // Queued from the message thread, drained inside the measured blocks.
    for (auto i = 0; i < plugin->parameters().size(); ++i)
        plugin->parameters()[i].setValue(0.25f);

    auto count = allocationsIn(
        [&]
        {
            for (auto block = 0; block < 32; ++block)
            {
                plugin->setParameterFromAudioThread(0, (block % 8) / 8.f);
                plugin->setParameterFromAudioThread(1, 0.f);
                plugin->process(context);
            }
        });

    check(count == 0);
};

auto tInstrument = test("Allocations/aHostedInstrumentsMidiTouchesNothing") = []
{
    const auto formats = DefaultFormats();
    auto plugin = loadFixture(formats, "HostFixtureInstrument");
    check(plugin != nullptr);

    if (plugin == nullptr)
        return;

    auto context = contextFor(*plugin);

    for (auto i = 0; i < 4; ++i)
        plugin->process(context);

    auto count = allocationsIn(
        [&]
        {
            for (auto block = 0; block < 32; ++block)
            {
                auto pitch = 60 + block % 12;
                context.clearMidi();
                context.midiIn[0].add(MIDI::Event::noteOn(0, pitch, 0.7f, 3));
                context.midiIn[0].add(MIDI::Event::controlChange(0, 1, 0.5f, 5));
                context.midiIn[0].add(MIDI::Event::noteOff(0, pitch, 0.f, 9));
                plugin->process(context);
            }
        });

    check(count == 0);
    check(context.midiOut[0].size() == 3);
};
} // namespace
