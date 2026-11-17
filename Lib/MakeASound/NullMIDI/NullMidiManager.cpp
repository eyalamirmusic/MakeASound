#include "NullMidiManager.h"

namespace MakeASound::NullMIDI
{

Error MidiManager::record(Error error) noexcept
{
    lastError.store(error, std::memory_order_relaxed);
    return error;
}

bool MidiManager::isAvailable() const
{
    return false;
}

Error MidiManager::getLastError() const
{
    return lastError.load(std::memory_order_relaxed);
}

Vector<MidiPortInfo> MidiManager::getInputPorts()
{
    return {};
}

Vector<MidiPortInfo> MidiManager::getOutputPorts()
{
    return {};
}

// A virtual port's id is rejected the way every backend rejects it, so a caller
// sees the same error for the same mistake whichever backend it got.
Error MidiManager::openInput(int portId, const MidiInputCallback&)
{
    if (portId < 0)
        return record(Error::INVALID_PARAMETER);

    return record(Error::SYSTEM_ERROR);
}

std::optional<int> MidiManager::openVirtualInput(const std::string&,
                                                 const MidiInputCallback&)
{
    record(Error::SYSTEM_ERROR);
    return std::nullopt;
}

void MidiManager::closeInput(int)
{
}

void MidiManager::closeAllInputs()
{
}

bool MidiManager::isInputOpen(int) const
{
    return false;
}

Vector<int> MidiManager::getOpenInputPorts() const
{
    return {};
}

// The audio thread's, and there is nothing to hand over: `out` gains nothing.
void MidiManager::drainMessages(Vector<MidiInputEvent>&)
{
}

Error MidiManager::openOutput(int)
{
    return record(Error::SYSTEM_ERROR);
}

Error MidiManager::openVirtualOutput(const std::string&)
{
    return record(Error::SYSTEM_ERROR);
}

void MidiManager::closeOutput()
{
}

bool MidiManager::isOutputOpen() const
{
    return false;
}

// What every backend answers for a send with no output open.
Error MidiManager::sendMessage(const std::uint8_t*, std::size_t)
{
    return record(Error::INVALID_USE);
}

} // namespace MakeASound::NullMIDI
