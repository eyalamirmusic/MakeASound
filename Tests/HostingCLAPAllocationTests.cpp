// A hosted CLAP plugin's audio-thread paths under the allocation ban: process()
// with its event lists (edits queued from the message thread, writes from the
// audio thread, the transport, MIDI in both dialects' raw form and sysex) and its
// output events (parameters, MIDI, sysex), and start_processing in the first
// block. The hosted plugins are the CLAP fixtures, so what is measured is the
// host side plus a plugin known to be clean. The busy driver is
// HostingAllocationDriver.h's, as for VST3 and Audio Units.

#include "HostingAllocationDriver.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <filesystem>
#include <string>

namespace HostingCLAPAllocationTests
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
    const auto path = (std::filesystem::path(MAKEASOUND_HOST_FIXTURE_CLAP_DIR)
                       / (std::string(name) + ".clap"))
                          .string();

    auto scan = formats.scanTarget(path).scan;
    check(scan.classes.size() == 1);

    if (scan.classes.empty())
        return {};

    return std::move(formats.load(scan.classes[0]).plugin);
}

auto tEffect = test("Allocations/aHostedCLAPEffectsBusyBlockTouchesNothing") = []
{
    const auto formats = DefaultFormats();
    auto plugin = loadFixture(formats, "HostFixtureEffect");
    check(plugin != nullptr);

    if (plugin != nullptr)
        check(busyBlocksAllocations(*plugin, blockSize) == 0);
};

auto tInstrument =
    test("Allocations/aHostedCLAPInstrumentsBusyBlockTouchesNothing") = []
{
    const auto formats = DefaultFormats();
    auto plugin = loadFixture(formats, "HostFixtureInstrument");
    check(plugin != nullptr);

    if (plugin != nullptr)
        check(busyBlocksAllocations(*plugin, blockSize) == 0);
};

auto tFirstBlock =
    test("Allocations/aHostedCLAPsFirstBlockStartsProcessingClean") = []
{
    const auto formats = DefaultFormats();
    auto plugin = loadFixture(formats, "HostFixtureEffect");
    check(plugin != nullptr);

    if (plugin == nullptr)
        return;

    // Prepared, never processed: the measured blocks include the one that calls
    // start_processing, and a re-prepare's first block does again.
    auto context = contextFor(*plugin, blockSize);
    auto count = allocationsOverBlocks(*plugin, context, [](int) {});
    check(count == 0);
};

auto tMidi = test("Allocations/aHostedCLAPInstrumentsMidiAndSysExTouchNothing") = []
{
    const auto formats = DefaultFormats();
    auto plugin = loadFixture(formats, "HostFixtureInstrument");
    check(plugin != nullptr);

    if (plugin == nullptr)
        return;

    auto context = contextFor(*plugin, blockSize);
    warmUp(*plugin, context);

    auto bytes = std::array<uint8_t, MIDI::SysEx::maxBytes> {};
    bytes.front() = 0xf0;
    bytes.back() = 0xf7;

    auto longest = static_cast<int>(bytes.size());
    auto& level = plugin->parameters()[0];

    auto count = allocationsOverBlocks(
        *plugin,
        context,
        [&](int block)
        {
            auto pitch = 60 + block % 12;
            bytes[1] = static_cast<uint8_t>(block & 0x7f);

            context.clearMidi();
            context.midiIn[0].add(MIDI::Event::noteOn(0, pitch, 0.7f, 3));
            context.midiIn[0].add(MIDI::Event::sysEx(bytes.data(), 6, 2));
            context.midiIn[0].add(MIDI::Event::pitchBend(0, 0.25f, 4));
            context.midiIn[0].add(MIDI::Event::sysEx(bytes.data(), longest, 5));
            context.midiIn[0].add(MIDI::Event::noteOff(0, pitch, 0.f, 9));

            // An edit queued from the message thread's side of the queue, and
            // one from the audio thread, in the same block.
            level.setValue((block % 4) / 4.f);
            plugin->setParameterFromAudioThread(0, (block % 8) / 8.f);
        });

    check(count == 0);
    check(context.midiOut[0].size() == 5);
};
} // namespace
} // namespace HostingCLAPAllocationTests
