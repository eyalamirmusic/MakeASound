// Tests for the facade against whatever backend the platform linked. Everything
// here needs a virtual port - the only loopback a runner with no MIDI hardware
// has - and returns without asserting where the platform has none (Windows; the
// iOS simulator, which refuses virtual endpoints to a bundle-less process; a
// Linux container with no /dev/snd/seq, where there is no sequencer to open at
// all), so a machine that cannot be measured reports the same as one that passed
// nothing: nothing measured.

#include <MakeASound/MakeASound.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// The Apple backend's timestamp conversion, reached directly: a loopback cannot
// exercise it on its own, and the library's include root is already on the path.
#if defined(__APPLE__)
    #include <MakeASound/CoreMIDI/CoreMIDI-Backend.h>
    #include <mach/mach_time.h>
#endif

using namespace nano;
using namespace std::chrono_literals;

using MakeASound::Error;
using MakeASound::MidiBlockSync;
using MakeASound::MidiEvents;
using MakeASound::MidiManager;
using MakeASound::MidiNotification;
using MakeASound::MIDI::Event;

namespace
{
using Clock = std::chrono::steady_clock;

const std::string loopbackName = "MakeASound Facade Test";

// A virtual input is a destination anything in the system can send to, our own
// output included: the platform's own loopback.
bool openLoopbackOutput(MidiManager& midi, const std::string& name)
{
    for (const auto& port: midi.getOutputPorts())
        if (port.name.find(name) != std::string::npos)
            return midi.openOutput(port.id) == Error::NoError;

    return false;
}

// Opens both ends of the loopback, or reports that this platform has none.
bool openLoopback(MidiManager& midi, const MakeASound::MidiInputCallback& cb = {})
{
    if (!midi.isAvailable())
        return false;

    auto portId = midi.openVirtualInput(loopbackName, cb);

    if (!portId.has_value())
        return false;

    if (openLoopbackOutput(midi, loopbackName))
        return true;

    midi.closeAllInputs();
    return false;
}

// Drains until `wanted` events have turned up, or the wait runs out.
int drainUntil(MidiManager& midi, MidiEvents& events, int wanted, Clock::duration t)
{
    auto deadline = Clock::now() + t;

    while (events.size() < wanted && Clock::now() < deadline)
    {
        std::this_thread::sleep_for(2ms);
        midi.drainMessages(events);
    }

    return events.size();
}

int countNotifications(MidiManager& midi, MidiNotification wanted, Clock::duration t)
{
    auto deadline = Clock::now() + t;
    auto seen = 0;

    while (seen == 0 && Clock::now() < deadline)
    {
        std::this_thread::sleep_for(10ms);

        for (auto notification: midi.drainNotifications())
            if (notification == wanted)
                ++seen;
    }

    return seen;
}

auto tAvailable = test("Midi/theAppleBackendComesUp") = []
{
    auto midi = MidiManager {};

#if defined(__APPLE__)
    // Core MIDI starts on both macOS and the iOS simulator, so Apple has no
    // machine where the MIDI system is simply absent.
    check(midi.isAvailable());
#else
    // Elsewhere a machine with no MIDI system at all is an ordinary state - a
    // Linux kernel with no snd-seq is exactly that - so all that is pinned is
    // that one which did come up says nothing went wrong.
    if (midi.isAvailable())
        check(midi.getLastError() == Error::NoError);
#endif
};

auto tDefaults = test("Midi/theSysExLimitsAreWhatTheyClaim") = []
{
    auto midi = MidiManager {};

    check(midi.getMaxSysExBytes() == 64 * 1024);

    midi.setMaxSysExBytes(4096);
    check(midi.getMaxSysExBytes() == 4096);

    // A cap below the shortest message would leave the callback nothing to
    // deliver into, so it is floored rather than honoured.
    midi.setMaxSysExBytes(0);
    check(midi.getMaxSysExBytes() >= 3);
};

#if defined(__APPLE__)
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
#endif

auto tArrival = test("Midi/aLoopbackArrivalSitsOnTheManagersClock") = []
{
    auto midi = MidiManager {};

    if (!openLoopback(midi))
        return;

    auto before = MidiManager::now();
    midi.sendMessage(Event::noteOn(0, 60, 1.f));
    auto after = MidiManager::now();

    auto events = MidiEvents {};
    auto delivered = drainUntil(midi, events, 1, 3s);

    midi.closeOutput();
    midi.closeAllInputs();

    if (delivered == 0)
        return;

    // The send stamps the packet with the platform's own clock, so the arrival
    // cannot predate the send. The other side is only bounded loosely: a loaded
    // runner can take a while to hand the packet over, and how long it took is
    // not what this is measuring.
    auto arrival = events[0].arrival;

    check(arrival >= before - 20ms);
    check(arrival <= after + 500ms);
};

auto tBlockSync = test("Midi/blockSyncOffsetsAreMonotonicAndInsideTheBlock") = []
{
    auto midi = MidiManager {};

    if (!openLoopback(midi))
        return;

    constexpr auto numSamples = 512;
    constexpr auto sampleRate = 48000;

    auto sync = MidiBlockSync {};

    // The first block only establishes the window; events are measured against
    // the one before them.
    sync.drainForBlock(midi, numSamples, sampleRate);

    auto sent = 0;

    for (auto i = 0; i < 32; ++i)
    {
        if (midi.sendMessage(Event::noteOn(0, 60 + (i % 12), 1.f)) == Error::NoError)
            ++sent;

        std::this_thread::sleep_for(1ms);
    }

    auto seen = 0;
    auto ordered = true;
    auto inRange = true;
    auto placed = 0;

    for (auto block = 0; block < 200 && seen < sent; ++block)
    {
        sync.drainForBlock(midi, numSamples, sampleRate);

        auto previous = -1;

        for (const auto& evt: sync.events())
        {
            auto offset = evt.event.sampleOffset;

            ordered = ordered && offset >= previous;
            inRange = inRange && offset >= 0 && offset < numSamples;
            placed += offset > 0 ? 1 : 0;

            previous = offset;
            ++seen;
        }

        std::this_thread::sleep_for(std::chrono::microseconds {
            numSamples * 1000000 / sampleRate});
    }

    midi.closeOutput();
    midi.closeAllInputs();

    if (seen == 0)
        return;

    check(ordered);
    check(inRange);

    // A block is ~10.7ms at this rate and the sends are 1ms apart, so the window
    // an event lands in is wide enough to place it somewhere other than its
    // start. All-zero offsets would pass the two checks above and mean nothing.
    check(placed > 0);
};

auto tSysExRoundTrip = test("Midi/aLongSysExComesBackWholeInCallbackMode") = []
{
    constexpr auto payload = 1024;

    auto received = std::vector<std::uint8_t> {};
    auto messages = std::atomic<int> {0};

    auto midi = MidiManager {};

    // Big enough for the dump, and set before the port opens, which is when the
    // assembly buffer is allocated.
    midi.setMaxSysExBytes(8192);

    if (!openLoopback(midi,
                      [&](const MakeASound::MidiMessage& message)
                      {
                          if (message.bytes.size() > 3)
                          {
                              received = message.bytes;
                              ++messages;
                          }
                      }))
        return;

    auto dump = std::vector<std::uint8_t> {};
    dump.reserve(payload);
    dump.push_back(0xF0);

    for (auto i = 0; i < payload - 2; ++i)
        dump.push_back(static_cast<std::uint8_t>(i % 128));

    dump.push_back(0xF7);

    midi.sendMessage(dump.data(), dump.size());

    auto deadline = Clock::now() + 3s;

    while (messages.load() == 0 && Clock::now() < deadline)
        std::this_thread::sleep_for(5ms);

    auto arrived = messages.load();
    auto copy = received;

    midi.closeOutput();
    midi.closeAllInputs();

    if (arrived == 0)
        return;

    check(copy.size() == dump.size());
    check(copy == dump);
};

auto tIgnoredTypes = test("Midi/timingClockIsFilteredUntilItIsAskedFor") = []
{
    auto filtered = std::atomic<int> {0};
    auto midi = MidiManager {};

    if (!openLoopback(midi,
                      [&](const MakeASound::MidiMessage& message)
                      {
                          if (!message.bytes.empty() && message.bytes[0] == 0xF8)
                              ++filtered;
                      }))
        return;

    const auto clock = std::uint8_t {0xF8};

    midi.sendMessage(&clock, 1);
    std::this_thread::sleep_for(200ms);

    auto whileFiltered = filtered.load();

    // Applies to a port that is already open, not only to the next one.
    midi.setIgnoredTypes(false, true);

    for (auto i = 0; i < 8; ++i)
        midi.sendMessage(&clock, 1);

    auto deadline = Clock::now() + 2s;

    while (filtered.load() == 0 && Clock::now() < deadline)
        std::this_thread::sleep_for(5ms);

    auto whileDelivered = filtered.load();

    midi.closeOutput();
    midi.closeAllInputs();

    check(whileFiltered == 0);

    // Nothing arriving at all is a platform that delivered no loopback, which is
    // the skip every case here shares.
    if (whileDelivered == 0)
        return;

    check(whileDelivered > 0);
};

auto tHotplug = test("Midi/aNewVirtualPortIsAnnouncedToEveryoneElse") = []
{
    auto watcher = MidiManager {};

    if (!watcher.isAvailable())
        return;

    // Whether this platform has virtual ports at all, asked before the watcher
    // starts counting.
    {
        auto probe = MidiManager {};

        if (!probe.openVirtualInput("MakeASound Hotplug Probe").has_value())
            return;
    }

    watcher.drainNotifications();

    auto added = 0;
    auto removed = 0;

    {
        auto other = MidiManager {};

        if (other.openVirtualOutput("MakeASound Hotplug Source") != Error::NoError)
            return;

        added = countNotifications(watcher, MidiNotification::PortAdded, 3s);
    }

    removed = countNotifications(watcher, MidiNotification::PortRemoved, 3s);

    // A platform that never delivers inside the wait is reported as untested
    // rather than failed: no CI runner has MIDI hardware to prove it otherwise.
    if (added == 0)
        return;

    check(added > 0);
    check(removed > 0);
};
} // namespace
