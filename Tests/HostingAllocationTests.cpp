// A hosted plugin's audio-thread paths under the allocation ban: process() with
// output parameters and the fallback playhead, setParameterFromAudioThread, edits
// queued from the message thread, and MIDI in and out. The hosted plugins are the
// fixtures, so what is measured is the host side plus a plugin known to be clean.
// The drivers are HostingAllocationDriver.h's, which a consumer's plugin runs
// through too.

#include "HostingAllocationDriver.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <string>

namespace HostingAllocationTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;
using namespace HostingAllocation;

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

auto tEffect = test("Allocations/aHostedEffectsBlockTouchesNothing") = []
{
    const auto formats = DefaultFormats();
    auto plugin = loadFixture(formats, "HostFixtureEffect");
    check(plugin != nullptr);

    if (plugin == nullptr)
        return;

    auto context = contextFor(*plugin, blockSize);
    auto playing = Playhead();
    playing.isPlaying = true;
    plugin->setFallbackPlayhead(playing);

    warmUp(*plugin, context);

    // Queued from the message thread, drained inside the measured blocks.
    for (auto i = 0; i < plugin->parameters().size(); ++i)
        plugin->parameters()[i].setValue(0.25f);

    auto count = allocationsOverBlocks(
        *plugin,
        context,
        [&](int block)
        {
            plugin->setParameterFromAudioThread(0, (block % 8) / 8.f);
            plugin->setParameterFromAudioThread(1, 0.f);
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

    auto context = contextFor(*plugin, blockSize);
    warmUp(*plugin, context);

    auto count = allocationsOverBlocks(
        *plugin,
        context,
        [&](int block)
        {
            auto pitch = 60 + block % 12;
            context.clearMidi();
            context.midiIn[0].add(MIDI::Event::noteOn(0, pitch, 0.7f, 3));
            context.midiIn[0].add(MIDI::Event::controlChange(0, 1, 0.5f, 5));
            context.midiIn[0].add(MIDI::Event::noteOff(0, pitch, 0.f, 9));
        });

    check(count == 0);
    check(context.midiOut[0].size() == 3);
};
} // namespace
} // namespace HostingAllocationTests
