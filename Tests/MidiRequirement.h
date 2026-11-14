#pragma once

// Whether a MIDI case that found nothing to measure has skipped or failed. Every
// case that needs a virtual-port loopback returns without asserting where the
// platform has none - Windows, whose WinMM has no virtual ports; the iOS
// simulator; a Linux box with no /dev/snd/seq - and the same where its loopback
// opened but nothing came back. MAKEASOUND_REQUIRE_MIDI=1 says this machine does
// have one (Linux CI loads snd-seq-dummy and sets it), so there each of those
// returns is a failure instead.
//
// Header-only and std-only apart from NanoTest and eacp Core, so the allocation
// suites can use it without the plugin core, and inline in a namespace of its
// own, so a unity batch can hold every file that includes it.

#include <NanoTest/NanoTest.h>

#include <cstdio>
#include <source_location>
#include <string_view>
#include <eacp/Core/Utils/Environment.h>

namespace MidiRequirement
{

inline bool isRequired()
{
    return eacp::getEnvValue("MAKEASOUND_REQUIRE_MIDI") == "1";
}

// Where a case would return without asserting, as `return unavailable("why");`:
// nothing unless MIDI is required, a failure naming `why` when it is.
inline void
    unavailable(std::string_view why,
                const std::source_location& where = std::source_location::current())
{
    if (isRequired())
        nano::check(false, why, where);
}

// Where a case has what it came to measure. Required, it says so on stdout, which
// is how a CI log shows the case ran against a real sequencer rather than passed
// by returning early.
inline void
    measured(const std::source_location& where = std::source_location::current())
{
    if (!isRequired())
        return;

    auto file = std::string_view {where.file_name()};
    file = file.substr(file.find_last_of("/\\") + 1);

    std::printf("midi-required: measured at %.*s:%d\n",
                static_cast<int>(file.size()),
                file.data(),
                static_cast<int>(where.line()));
}

} // namespace MidiRequirement
