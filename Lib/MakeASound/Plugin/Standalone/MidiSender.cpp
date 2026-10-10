#include "MidiSender.h"

#include <chrono>

namespace MakeASound::Standalone
{

MidiSender::MidiSender(MidiManager& midiToUse)
    : midi(midiToUse)
{
}

MidiSender::~MidiSender()
{
    stop();
}

bool MidiSender::push(const MIDI::Event& event) noexcept
{
    return queue.push(event);
}

void MidiSender::start()
{
    if (thread.joinable())
        return;

    // What queued up while stopped was meant for a port that is gone.
    auto stale = MIDI::Event {};

    while (queue.pop(stale))
    {
    }

    running = true;
    thread = std::thread([this] { run(); });
}

void MidiSender::stop()
{
    if (!thread.joinable())
        return;

    running = false;
    thread.join();
}

void MidiSender::run()
{
    using namespace std::chrono_literals;

    auto event = MIDI::Event {};

    while (running)
    {
        while (queue.pop(event))
            if (midi.isOutputOpen())
                midi.sendMessage(event);

        std::this_thread::sleep_for(1ms);
    }
}

} // namespace MakeASound::Standalone
