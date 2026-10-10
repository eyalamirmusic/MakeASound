#pragma once

#include "../../MIDI/MidiManager.h"
#include "../../Realtime/SPSCQueue.h"

#include <atomic>
#include <thread>

namespace MakeASound::Standalone
{

// Takes the plugin's MIDI out off the audio thread, where sending is not safe,
// and sends it from a thread of its own every millisecond. Events that arrive
// while no output is open are dropped.
class MidiSender
{
public:
    explicit MidiSender(MidiManager& midiToUse);
    ~MidiSender();

    MidiSender(const MidiSender&) = delete;
    MidiSender& operator=(const MidiSender&) = delete;

    // Audio thread. False when the queue is full.
    bool push(const MIDI::Event& event) noexcept;

    // Host thread; both idempotent. Stop around opening or closing the output,
    // since MidiManager does not guard a send against either.
    void start();
    void stop();
    bool isRunning() const noexcept { return thread.joinable(); }

private:
    void run();

    MidiManager& midi;
    SPSCQueue<MIDI::Event, 1024> queue;
    std::atomic<bool> running {false};
    std::thread thread;
};

} // namespace MakeASound::Standalone
