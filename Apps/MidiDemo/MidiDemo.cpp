#include <MakeASound/MakeASound.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <thread>

namespace MS = MakeASound;

namespace
{
// Flipped by SIGINT (Ctrl-C) so the send loop can unwind cleanly and let
// the MidiManager destructor close both ports.
std::atomic<bool> running {true};

void onSignal(int)
{
    running = false;
}

MS::MidiMessage noteOn(int channel, int pitch, int velocity)
{
    auto msg = MS::MidiMessage {};
    msg.bytes = {static_cast<std::uint8_t>(0x90 | (channel & 0x0F)),
                 static_cast<std::uint8_t>(pitch & 0x7F),
                 static_cast<std::uint8_t>(velocity & 0x7F)};
    return msg;
}

MS::MidiMessage noteOff(int channel, int pitch)
{
    auto msg = MS::MidiMessage {};
    msg.bytes = {static_cast<std::uint8_t>(0x80 | (channel & 0x0F)),
                 static_cast<std::uint8_t>(pitch & 0x7F),
                 std::uint8_t {0}};
    return msg;
}
} // namespace

int main()
{
    std::signal(SIGINT, onSignal);

    auto midi = MS::MidiManager {};

    // Virtual ports are visible to other apps (DAWs, MIDI Monitor, etc.).
    // Connect "MakeASound Demo Out" to "MakeASound Demo In" in your MIDI
    // router to watch the notes below loop straight back into the logger.
    midi.openVirtualOutput("MakeASound Demo Out");

    midi.openVirtualInput("MakeASound Demo In",
                          [](const MS::MidiMessage& message)
                          { std::cout << "  in  < " << MS::formatMessage(message)
                                      << '\n'; });

    std::cout << "Virtual MIDI ports open:\n"
              << "  out : MakeASound Demo Out\n"
              << "  in  : MakeASound Demo In\n"
              << "Sending a note every second (note off 0.5s later). "
              << "Press Ctrl-C to quit.\n\n";

    constexpr auto channel = 0;
    constexpr auto velocity = 100;
    auto pitch = 60; // middle C, walking up a C major scale

    while (running)
    {
        auto on = noteOn(channel, pitch, velocity);
        midi.sendMessage(on);
        std::cout << "  out > " << MS::formatMessage(on) << '\n';

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!running)
        {
            midi.sendMessage(noteOff(channel, pitch));
            break;
        }

        midi.sendMessage(noteOff(channel, pitch));

        pitch += 2;
        if (pitch > 72)
            pitch = 60;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\nStopping.\n";
    return 0;
}
