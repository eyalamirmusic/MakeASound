#include "MidiManager.h"
#include "../RTMidi/RTMidiManager.h"

namespace MakeASound
{

MidiManager::MidiManager()
    : pimpl(std::make_unique<RTMidi::MidiManager>())
{
}

MidiManager::~MidiManager()
{
    closeInput();
    closeOutput();
}

std::vector<MidiPortInfo> MidiManager::getInputPorts() const
{
    return pimpl->getInputPorts();
}

std::vector<MidiPortInfo> MidiManager::getOutputPorts() const
{
    return pimpl->getOutputPorts();
}

void MidiManager::openInput(unsigned int portId, const MidiInputCallback& cb)
{
    pimpl->openInput(portId, cb);
}

void MidiManager::closeInput()
{
    pimpl->closeInput();
}

bool MidiManager::isInputOpen() const
{
    return pimpl->isInputOpen();
}

void MidiManager::openOutput(unsigned int portId)
{
    pimpl->openOutput(portId);
}

void MidiManager::closeOutput()
{
    pimpl->closeOutput();
}

bool MidiManager::isOutputOpen() const
{
    return pimpl->isOutputOpen();
}

void MidiManager::sendMessage(const MidiMessage& message)
{
    if (!message.bytes.empty())
        pimpl->sendMessage(message.bytes.data(), message.bytes.size());
}

void MidiManager::sendMessage(const std::uint8_t* bytes, std::size_t size)
{
    pimpl->sendMessage(bytes, size);
}

} // namespace MakeASound
