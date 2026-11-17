// The Core MIDI backend, held to what only Apple promises: the client always
// comes up, and its timestamp conversion lands on MidiManager::now()'s clock.
// Built only when the device library links Core MIDI (see Tests/CMakeLists.txt),
// since it reaches the backend's own header directly; the library's include root
// is already on the path.

#include <MakeASound/MakeASound.h>
#include <MakeASound/CoreMIDI/CoreMIDI-Backend.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <mach/mach_time.h>

namespace CoreMIDIBackendTests
{
using namespace nano;
using namespace std::chrono_literals;

using MakeASound::MidiManager;

namespace
{
using Clock = std::chrono::steady_clock;

auto tAvailable = test("Midi/theAppleBackendComesUp") = []
{
    auto midi = MidiManager {};

    // Core MIDI starts on both macOS and the iOS simulator, so Apple has no
    // machine where the MIDI system is simply absent. A failure names the error,
    // and Core MIDI's own status on a client asked for again from here.
    if (!midi.isAvailable())
    {
        auto probe = MIDIClientRef {};
        auto status = MIDIClientCreate(CFSTR("Probe"), nullptr, nullptr, &probe);

        if (status == noErr)
            MIDIClientDispose(probe);

        check(false,
              "MidiManager unavailable: lastError "
                  + std::to_string(static_cast<int>(midi.getLastError()))
                  + ", MIDIClientCreate now returns " + std::to_string(status));
    }
};

auto tClockConversion = test("Midi/aMachTimestampConvertsToTheManagersClock") = []
{
    // mach_absolute_time and steady_clock are different counters — the second
    // keeps running while the machine sleeps and the first does not — so the
    // backend measures the offset between them instead of assuming it is zero.
    // A stamp taken right now has to come back as right now.
    auto worst = Clock::duration {};

    for (auto i = 0; i < 16; ++i)
    {
        auto converted = MakeASound::CoreMIDI::toTimePoint(mach_absolute_time());
        auto now = MidiManager::now();
        auto delta = now > converted ? now - converted : converted - now;

        worst = std::max(worst, delta);
    }

    check(worst < 5ms);

    // A stamp of 0 is Core MIDI's "as soon as you can", which is also now.
    check(MidiManager::now() - MakeASound::CoreMIDI::toTimePoint(0) < 5ms);
};
} // namespace
} // namespace CoreMIDIBackendTests
