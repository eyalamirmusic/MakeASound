#include <MakeASound/MakeASound.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace MS = MakeASound;
namespace MIDI = MakeASound::MIDI;

namespace
{
// Flipped by SIGINT (Ctrl-C) so the send loop can unwind cleanly and let
// the MidiManager destructor close both ports.
std::atomic<bool> running {true};

void onSignal(int)
{
    running = false;
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

    midi.openVirtualInput(
        "MakeASound Demo In",
        [](const MS::MidiMessage& message)
        {
            // Decode the raw bytes into a typed MIDI::Event for logging.
            auto event = MIDI::convertMidi(message.bytes.data(),
                                           static_cast<int>(message.bytes.size()));
            std::cout << "  in  < "
                      << (event ? MIDI::toString(*event)
                                : MS::formatMessage(message))
                      << '\n';
        });

    std::cout << "Virtual MIDI ports open:\n"
              << "  out : MakeASound Demo Out\n"
              << "  in  : MakeASound Demo In\n"
              << "Sending a note every second (note off 0.5s later). "
              << "Press Ctrl-C to quit.\n\n";

    constexpr auto channel = 0;
    constexpr auto velocity = 100.f / 127.f; // normalized 0..1
    auto pitch = 60; // middle C, walking up a C major scale

    while (running)
    {
        auto on = MIDI::Event::noteOn(channel, pitch, velocity);
        midi.sendMessage(on);
        std::cout << "  out > " << MIDI::toString(on) << '\n';

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        midi.sendMessage(MIDI::Event::noteOff(channel, pitch, 0.f));
        if (!running)
            break;

        pitch += 2;
        if (pitch > 72)
            pitch = 60;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\nStopping.\n";
    return 0;
}
