#include "RTMidiManager.h"

#include <algorithm>

namespace MakeASound::RTMidi
{

MidiManager::MidiManager()
    : inputEnumerator(std::make_unique<::RtMidiIn>())
    , outputEnumerator(std::make_unique<::RtMidiOut>())
    , output(std::make_unique<::RtMidiOut>())
{
}

std::vector<MidiPortInfo> MidiManager::getInputPorts()
{
    return getPorts(*inputEnumerator);
}

std::vector<MidiPortInfo> MidiManager::getOutputPorts()
{
    return getPorts(*outputEnumerator);
}

void MidiManager::openInput(unsigned int portId, const MidiInputCallback& cb)
{
    closeInput(portId);

    auto port = std::make_unique<InputPort>();
    port->portId = portId;
    port->callback = cb;
    port->rtIn = std::make_unique<::RtMidiIn>();
    port->rtIn->setCallback(midiInputTrampoline, port.get());
    port->rtIn->openPort(portId);

    inputs.push_back(std::move(port));
}

void MidiManager::closeInput(unsigned int portId)
{
    std::erase_if(inputs,
                  [portId](auto& p) { return p->portId == portId; });
}

void MidiManager::closeAllInputs()
{
    inputs.clear();
}

bool MidiManager::isInputOpen(unsigned int portId) const
{
    return std::ranges::any_of(inputs,
                               [portId](auto& p) { return p->portId == portId; });
}

std::vector<unsigned int> MidiManager::getOpenInputPorts() const
{
    auto result = std::vector<unsigned int> {};
    result.reserve(inputs.size());

    for (auto& p: inputs)
        result.push_back(p->portId);

    return result;
}

void MidiManager::drainMessages(std::vector<MidiInputEvent>& out)
{
    for (auto& port: inputs)
    {
        if (port->callback)
            continue;

        if (!port->lock.tryLock())
            continue;

        for (auto& msg: port->queue)
            out.push_back(MidiInputEvent {port->portId, std::move(msg)});

        port->queue.clear();
        port->lock.unlock();
    }
}

void MidiManager::openOutput(unsigned int portId)
{
    closeOutput();
    output->openPort(portId);
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
    if (message == nullptr || userData == nullptr)
        return;

    auto& port = *static_cast<InputPort*>(userData);
    auto msg = getMessage(timestamp, *message);

    if (port.callback)
    {
        port.callback(msg);
        return;
    }

    auto guard = EA::Locks::ScopedSpinLock {port.lock};
    port.queue.push_back(std::move(msg));
}

} // namespace MakeASound::RTMidi
