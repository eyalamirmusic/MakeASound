// The other half of the allocation question: the threads we don't own. The pure
// paths in AllocationTests.cpp can be measured by calling them; a real audio
// callback and the platform's MIDI thread can only be measured from inside
// themselves, so the ban is raised and lowered there and the count comes back
// through an atomic.
//
// Everything here needs the platform to actually give us something - a playback
// device, a virtual MIDI port - and returns without asserting when it doesn't, so a
// headless runner reports the same as a machine with no soundcard: nothing measured.

#include "AllocationProbe.h"

#include <MakeASound/MakeASound.h>

#include <NanoTest/NanoTest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace nano;
using namespace std::chrono_literals;

using MakeASound::AudioCallbackInfo;
using MakeASound::Backend;
using MakeASound::DeviceManager;
using MakeASound::Error;
using MakeASound::MidiEvents;
using MakeASound::MidiManager;
using MakeASound::MidiMessage;
using MakeASound::MidiNotification;
using MakeASound::MIDI::Event;
using MakeASound::MIDI::SysEx;

namespace
{
using Clock = std::chrono::steady_clock;

// Spins until the foreign thread has run `wanted` times, or gives up.
bool waitForVisits(Probe::ThreadProbe& probe, int wanted, Clock::duration timeout)
{
    auto deadline = Clock::now() + timeout;

    while (probe.visits.load() < wanted)
    {
        if (Clock::now() > deadline)
            return false;

        std::this_thread::sleep_for(2ms);
    }

    return true;
}

// The ban is thread-local, so only the watched thread can lift it - stop measuring
// and let it run a few more times before anything tears it down.
void releaseTheWatchedThread(Probe::ThreadProbe& probe)
{
    auto seen = probe.visits.load();

    probe.measuring = false;
    waitForVisits(probe, seen + 3, 1s);
}

const std::string probePortName = "MakeASound Allocation Probe";
const std::string queuePortName = "MakeASound Allocation Queue";

bool openOutputNamed(MidiManager& midi, const std::string& name)
{
    for (const auto& port: midi.getOutputPorts())
        if (port.name.find(name) != std::string::npos)
            return midi.openOutput(port.id) == Error::NoError;

    return false;
}

// A virtual input is a destination anything in the system can send to, our own
// output included: the platform's own loopback, and the only way to get real
// messages onto the platform's MIDI thread with no hardware attached.
bool openLoopbackOutput(MidiManager& midi)
{
    return openOutputNamed(midi, probePortName);
}

void sendNotes(MidiManager& midi, int count)
{
    for (auto i = 0; i < count; ++i)
        midi.sendMessage(Event::noteOn(0, 60 + (i % 12), 1.f));
}

// A well-formed dump of `size` bytes, 0xF0 and 0xF7 included.
std::vector<std::uint8_t> makeSysEx(int size)
{
    auto dump = std::vector<std::uint8_t>(static_cast<std::size_t>(size), 0);

    dump.front() = 0xF0;
    dump.back() = 0xF7;

    for (auto i = 1; i < size - 1; ++i)
        dump[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i % 128);

    return dump;
}

void sendSysEx(MidiManager& midi, const std::vector<std::uint8_t>& dump, int times)
{
    for (auto i = 0; i < times; ++i)
        midi.sendMessage(dump.data(), dump.size());
}

auto tAudioThread = test("Allocations/theAudioCallbackThreadStaysOffTheHeap") = []
{
    // Declared first so it outlives the manager: a thread still carrying our ban
    // must not find the asserting default handler on the way down.
    auto probe = Probe::ThreadProbe {};
    auto manager = DeviceManager {};

    auto config = manager.getDefaultOutputConfig();

    if (!config.output.has_value())
        return;

    // PulseAudio has no audio thread of its own to lend us: the callback runs on
    // miniaudio's worker, which is also the thread that speaks the daemon's socket
    // protocol, and libpulse heap-allocates every packet of that. What the probe
    // would count there is not this library's, so nothing is measured.
    if (manager.getBackend() == Backend::PulseAudio)
        return;

    // mark() runs at the end of the callback, so the window it opens covers the rest
    // of this block - the re-interleave and the bookkeeping behind it - plus all of
    // the next one, up to and including the facade's shape comparison.
    auto error = manager.start(config,
                               [&probe](AudioCallbackInfo& info)
                               {
                                   for (auto channel: info.getOutput())
                                       channel.fill(0.f);

                                   probe.mark();
                               });

    if (error != Error::NoError)
        return;

    auto ran = waitForVisits(probe, 64, 3s);

    releaseTheWatchedThread(probe);

    auto violations = probe.violations.load();
    auto blocks = probe.visits.load();

    manager.stop();

    // A device that opened but never called back measured nothing, and saying so
    // beats a green tick.
    if (!ran && blocks < 2)
        return;

    check(violations == 0);
};

auto tMidiInputThread = test("Allocations/theMidiInputThreadStaysOffTheHeap") = []
{
    auto probe = Probe::ThreadProbe {};
    auto midi = MidiManager {};

    if (!midi.isAvailable())
        return;

    // Callback mode: the platform hands the message straight to us on its MIDI
    // thread, which is the thread this test is about.
    auto portId = midi.openVirtualInput(
        probePortName, [&probe](const MidiMessage&) { probe.mark(); });

    if (!portId.has_value())
        return;

    if (!openLoopbackOutput(midi))
    {
        midi.closeAllInputs();
        return;
    }

    sendNotes(midi, 128);

    auto ran = waitForVisits(probe, 32, 3s);

    // Keep feeding it while it lifts the ban - a thread with nothing to deliver
    // never runs again, and only it can lift its own flag.
    probe.measuring = false;
    sendNotes(midi, 8);

    auto seen = probe.visits.load();
    waitForVisits(probe, seen + 4, 1s);

    auto violations = probe.violations.load();
    auto messages = probe.visits.load();

    midi.closeOutput();
    midi.closeAllInputs();

    if (!ran && messages < 2)
        return;

    check(violations == 0);
};

auto tMidiSysEx = test("Allocations/aLongSysExOnTheMidiThreadStaysOffTheHeap") = []
{
    auto probe = Probe::ThreadProbe {};
    auto midi = MidiManager {};

    if (!midi.isAvailable())
        return;

    // The assembly buffer is allocated when the port opens, so the size the
    // dumps need is asked for before that and never again.
    midi.setMaxSysExBytes(8192);

    auto portId = midi.openVirtualInput(
        probePortName, [&probe](const MidiMessage&) { probe.mark(); });

    if (!portId.has_value())
        return;

    if (!openLoopbackOutput(midi))
    {
        midi.closeAllInputs();
        return;
    }

    // A kilobyte arrives as a run of packets, so this measures the assembly the
    // parser does across them as well as the copy into the callback's buffer.
    auto dump = makeSysEx(1024);
    sendSysEx(midi, dump, 48);

    auto ran = waitForVisits(probe, 16, 3s);

    probe.measuring = false;
    sendSysEx(midi, dump, 4);

    auto seen = probe.visits.load();
    waitForVisits(probe, seen + 2, 1s);

    auto violations = probe.violations.load();
    auto messages = probe.visits.load();

    midi.closeOutput();
    midi.closeAllInputs();

    if (!ran && messages < 2)
        return;

    check(violations == 0);
};

auto tQueuedSysEx = test("Allocations/theOversizeSysExDropTouchesNothing") = []
{
    auto probe = Probe::ThreadProbe {};
    auto midi = MidiManager {};

    if (!midi.isAvailable())
        return;

    midi.setMaxSysExBytes(8192);

    // Two ports on one manager is one client, which the platform gives one
    // receive thread: the callback port raises the ban and the queue port's work
    // lands inside the window it opened. Somewhere that gave a client a thread
    // per port this would measure less, never something untrue.
    auto queuePort = midi.openVirtualInput(queuePortName);

    auto markPort = midi.openVirtualInput(
        probePortName, [&probe](const MidiMessage&) { probe.mark(); });

    if (!queuePort.has_value() || !markPort.has_value())
        return;

    auto sender = MidiManager {};

    if (!openOutputNamed(sender, queuePortName) || !openLoopbackOutput(midi))
    {
        midi.closeAllInputs();
        return;
    }

    // Longer than an Event can carry and shorter than the port's assembly
    // buffer: the parser hands over a whole dump and the queue tier drops it.
    auto dump = makeSysEx(SysEx::maxBytes * 8);
    auto sending = std::atomic<bool> {true};

    auto pump = std::thread(
        [&]
        {
            while (sending.load())
            {
                sendSysEx(sender, dump, 2);
                sendNotes(sender, 8);
                std::this_thread::sleep_for(1ms);
            }
        });

    for (auto i = 0; i < 40 && probe.visits.load() < 32; ++i)
        sendNotes(midi, 16);

    auto ran = waitForVisits(probe, 32, 3s);

    probe.measuring = false;
    sendNotes(midi, 8);

    auto seen = probe.visits.load();
    waitForVisits(probe, seen + 4, 1s);

    sending = false;
    pump.join();

    auto violations = probe.violations.load();
    auto messages = probe.visits.load();

    auto dropped = 0;

    // The flag the receive thread raised is swept into a notification off that
    // thread, so it takes a moment to turn up.
    for (auto i = 0; i < 20 && dropped == 0; ++i)
    {
        std::this_thread::sleep_for(20ms);

        for (auto notification: midi.drainNotifications())
            if (notification == MidiNotification::SysExDropped)
                ++dropped;
    }

    auto events = MidiEvents {};
    midi.drainMessages(events);

    sender.closeOutput();
    midi.closeOutput();
    midi.closeAllInputs();

    if (!ran && messages < 2)
        return;

    check(violations == 0);

    // Nothing dropped would mean the oversize tier was never reached and the
    // measurement above proved nothing about it.
    check(dropped > 0);

    // The notes sent alongside the dumps still made it through the queue.
    check(events.size() > 0);
};

auto tQueuedDrain = test("Allocations/drainingRealQueuedMidiTouchesNothing") = []
{
    // Queue mode is what an audio callback uses: the MIDI thread parks the events
    // and drainMessages moves them across. This is that move, with real messages in
    // the queue rather than an empty port list.
    auto midi = MidiManager {};

    if (!midi.isAvailable())
        return;

    auto portId = midi.openVirtualInput(probePortName);

    if (!portId.has_value())
        return;

    if (!openLoopbackOutput(midi))
    {
        midi.closeAllInputs();
        return;
    }

    constexpr auto sent = 64;
    sendNotes(midi, sent);

    auto events = MidiEvents {};
    auto deadline = Clock::now() + 3s;

    // Nothing observes the port's queue without emptying it, so the wait is the
    // drain itself; the measured one below runs on a second batch.
    while (events.size() < sent && Clock::now() < deadline)
    {
        std::this_thread::sleep_for(5ms);
        midi.drainMessages(events);
    }

    auto delivered = events.size();

    if (delivered == 0)
    {
        midi.closeOutput();
        midi.closeAllInputs();
        return;
    }

    sendNotes(midi, sent);
    std::this_thread::sleep_for(200ms);

    auto drained = 0;

    auto count = Probe::allocationsIn(
        [&]
        {
            events.clear();
            midi.drainMessages(events);
            drained = events.size();
        });

    midi.closeOutput();
    midi.closeAllInputs();

    check(count == 0);
    check(drained > 0);
};
} // namespace
