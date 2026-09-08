// The other half of the allocation question: the threads we don't own. The pure
// paths in AllocationTests.cpp can be measured by calling them; a real audio
// callback and RtMidi's input thread can only be measured from inside themselves,
// so the ban is raised and lowered there and the count comes back through an atomic.
//
// Everything here needs the platform to actually give us something - a playback
// device, a virtual MIDI port - and returns without asserting when it doesn't, so a
// headless runner reports the same as a machine with no soundcard: nothing measured.

#include "AllocationProbe.h"

#include <MakeASound/MakeASound.h>

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <optional>
#include <string>
#include <thread>

using namespace nano;
using namespace std::chrono_literals;

using MakeASound::AudioCallbackInfo;
using MakeASound::Backend;
using MakeASound::DeviceManager;
using MakeASound::Error;
using MakeASound::MidiEvents;
using MakeASound::MidiManager;
using MakeASound::MIDI::Event;

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

// A virtual input is a destination anything in the system can send to, our own
// output included: the platform's own loopback, and the only way to get real
// messages onto RtMidi's input thread with no hardware attached.
bool openLoopbackOutput(MidiManager& midi)
{
    for (const auto& port: midi.getOutputPorts())
        if (port.name.find(probePortName) != std::string::npos)
            return midi.openOutput(port.id) == Error::NoError;

    return false;
}

void sendNotes(MidiManager& midi, int count)
{
    for (auto i = 0; i < count; ++i)
        midi.sendMessage(Event::noteOn(0, 60 + (i % 12), 1.f));
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
                                   for (auto channel: info.getOutput().channels())
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

    // Callback mode: RtMidi hands the message straight to us on its input thread,
    // which is the thread this test is about.
    auto portId = midi.openVirtualInput(probePortName,
                                        [&probe](const MakeASound::MidiMessage&)
                                        { probe.mark(); });

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

auto tQueuedDrain = test("Allocations/drainingRealQueuedMidiTouchesNothing") = []
{
    // Queue mode is what an audio callback uses: RtMidi's thread parks the events
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
