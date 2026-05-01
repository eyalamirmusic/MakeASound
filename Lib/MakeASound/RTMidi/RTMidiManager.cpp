#include "RTMidiManager.h"
#include "../MIDI/MIDI.h"

namespace MakeASound::RTMidi
{

MidiManager::MidiManager()
    : inputEnumerator(EA::makeOwned<::RtMidiIn>())
    , outputEnumerator(EA::makeOwned<::RtMidiOut>())
    , output(EA::makeOwned<::RtMidiOut>())
{
}

Vector<MidiPortInfo> MidiManager::getInputPorts()
{
    return getPorts(*inputEnumerator);
}

Vector<MidiPortInfo> MidiManager::getOutputPorts()
{
    return getPorts(*outputEnumerator);
}

void MidiManager::openInput(int portId, const MidiInputCallback& cb)
{
    closeInput(portId);

    auto& port = inputs.createNew();
    port.portId = portId;
    port.callback = cb;
    port.rtIn = EA::makeOwned<::RtMidiIn>();
    port.rtIn->setCallback(midiInputTrampoline, &port);
    port.rtIn->openPort(static_cast<unsigned int>(portId));
}

void MidiManager::closeInput(int portId)
{
    inputs.eraseIf([portId](auto& p) { return p->portId == portId; });
}

void MidiManager::closeAllInputs()
{
    inputs.clear();
}

bool MidiManager::isInputOpen(int portId) const
{
    for (auto& p: inputs)
        if (p->portId == portId)
            return true;

    return false;
}

Vector<int> MidiManager::getOpenInputPorts() const
{
    auto result = Vector<int> {};
    result.reserve(inputs.size());

    for (auto& p: inputs)
        result.add(p->portId);

    return result;
}

void MidiManager::drainMessages(Vector<MidiInputEvent>& out)
{
    for (auto& port: inputs)
    {
        if (port->callback)
            continue;

        if (!port->lock.tryLock())
            continue;

        for (auto& evt: port->queue)
            out.add(evt);

        port->queue.clear();
        port->lock.unlock();
    }
}

void MidiManager::openOutput(int portId)
{
    closeOutput();
    output->openPort(static_cast<unsigned int>(portId));
}

void MidiManager::closeOutput()
{
    if (output->isPortOpen())
        output->closePort();
}

bool MidiManager::isOutputOpen() const
{
    return output->isPortOpen();
}

void MidiManager::sendMessage(const std::uint8_t* bytes, std::size_t size)
{
    output->sendMessage(bytes, size);
}

void midiInputTrampoline(double timestamp,
                         std::vector<unsigned char>* message,
                         void* userData)
{
    auto arrival = std::chrono::steady_clock::now();

    if (message == nullptr || userData == nullptr)
        return;

    auto& port = *static_cast<InputPort*>(userData);

    if (port.callback)
    {
        auto msg = MidiMessage {};
        msg.timestamp = timestamp;
        msg.bytes.assign(message->begin(), message->end());
        port.callback(msg);
        return;
    }

    auto typed =
        MIDI::convertMidi(message->data(), static_cast<int>(message->size()));
    if (!typed)
        return;

    auto event = MidiInputEvent {};
    event.portId = port.portId;
    event.event = *typed;
    event.arrival = arrival;

    auto guard = EA::Locks::ScopedSpinLock {port.lock};
    port.queue.add(event);
}

} // namespace MakeASound::RTMidi
