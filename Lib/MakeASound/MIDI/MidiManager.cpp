#include "MidiManager.h"
#include "../RTMidi/RTMidiManager.h"

namespace MakeASound
{

MidiManager::MidiManager()
    : pimpl(EA::makeOwned<RTMidi::MidiManager>())
{
}

MidiManager::~MidiManager()
{
    closeAllInputs();
    closeOutput();
}

Vector<MidiPortInfo> MidiManager::getInputPorts() const
{
    return pimpl->getInputPorts();
}

Vector<MidiPortInfo> MidiManager::getOutputPorts() const
{
    return pimpl->getOutputPorts();
}

void MidiManager::openInput(int portId)
{
    pimpl->openInput(portId, nullptr);
}

void MidiManager::openInput(int portId, const MidiInputCallback& cb)
{
    pimpl->openInput(portId, cb);
}

int MidiManager::openVirtualInput(const std::string& name)
{
    return pimpl->openVirtualInput(name, nullptr);
}

int MidiManager::openVirtualInput(const std::string& name,
                                  const MidiInputCallback& cb)
{
    return pimpl->openVirtualInput(name, cb);
}

void MidiManager::closeInput(int portId)
{
    pimpl->closeInput(portId);
}

void MidiManager::closeAllInputs()
{
    pimpl->closeAllInputs();
}

bool MidiManager::isInputOpen(int portId) const
{
    return pimpl->isInputOpen(portId);
}

Vector<int> MidiManager::getOpenInputPorts() const
{
    return pimpl->getOpenInputPorts();
}

void MidiManager::drainMessages(MidiEvents& out)
{
    pimpl->drainMessages(out.raw());
}

void MidiManager::openOutput(int portId)
{
    pimpl->openOutput(portId);
}

void MidiManager::openVirtualOutput(const std::string& name)
{
    pimpl->openVirtualOutput(name);
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

void MidiManager::sendMessage(const MIDI::Event& event)
{
    auto raw = MIDI::toBytes(event);
    if (raw.size > 0)
        pimpl->sendMessage(raw.data.data(), static_cast<std::size_t>(raw.size));
}

} // namespace MakeASound
