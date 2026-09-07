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

bool MidiManager::isAvailable() const
{
    return pimpl->isAvailable();
}

Error MidiManager::getLastError() const
{
    return pimpl->getLastError();
}

Error MidiManager::openInput(int portId)
{
    return pimpl->openInput(portId, nullptr);
}

Error MidiManager::openInput(int portId, const MidiInputCallback& cb)
{
    return pimpl->openInput(portId, cb);
}

std::optional<int> MidiManager::openVirtualInput(const std::string& name)
{
    return pimpl->openVirtualInput(name, nullptr);
}

std::optional<int> MidiManager::openVirtualInput(const std::string& name,
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

Error MidiManager::openOutput(int portId)
{
    return pimpl->openOutput(portId);
}

Error MidiManager::openVirtualOutput(const std::string& name)
{
    return pimpl->openVirtualOutput(name);
}

void MidiManager::closeOutput()
{
    pimpl->closeOutput();
}

bool MidiManager::isOutputOpen() const
{
    return pimpl->isOutputOpen();
}

Error MidiManager::sendMessage(const MidiMessage& message)
{
    if (message.bytes.empty())
        return Error::INVALID_PARAMETER;

    return pimpl->sendMessage(message.bytes.data(), message.bytes.size());
}

Error MidiManager::sendMessage(const std::uint8_t* bytes, std::size_t size)
{
    return pimpl->sendMessage(bytes, size);
}

Error MidiManager::sendMessage(const MIDI::Event& event)
{
    auto raw = MIDI::toBytes(event);

    if (raw.size == 0)
        return Error::INVALID_PARAMETER;

    return pimpl->sendMessage(raw.data.data(), static_cast<std::size_t>(raw.size));
}

} // namespace MakeASound
