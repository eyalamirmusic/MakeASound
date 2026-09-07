#include "RTMidiManager.h"
#include "../MIDI/MIDI.h"

namespace MakeASound::RTMidi
{
namespace
{
Error toError(::RtMidiError::Type type)
{
    switch (type)
    {
        // RtMidi warns where it carries on — an already-open port, an ignored
        // message — which is not a failure to report.
        case ::RtMidiError::WARNING:
        case ::RtMidiError::DEBUG_WARNING:
            return Error::NoError;
        case ::RtMidiError::NO_DEVICES_FOUND:
            return Error::NO_DEVICES_FOUND;
        case ::RtMidiError::INVALID_DEVICE:
            return Error::INVALID_DEVICE;
        case ::RtMidiError::MEMORY_ERROR:
            return Error::MEMORY_ERROR;
        case ::RtMidiError::INVALID_PARAMETER:
            return Error::INVALID_PARAMETER;
        case ::RtMidiError::INVALID_USE:
            return Error::INVALID_USE;
        case ::RtMidiError::DRIVER_ERROR:
            return Error::DRIVER_ERROR;
        case ::RtMidiError::SYSTEM_ERROR:
            return Error::SYSTEM_ERROR;
        case ::RtMidiError::THREAD_ERROR:
            return Error::THREAD_ERROR;
        case ::RtMidiError::UNSPECIFIED:
        default:
            return Error::UNKNOWN_ERROR;
    }
}

// With a callback installed RtMidi reports through it and returns, instead of
// printing to stderr and throwing. Every path out of RtMidi still bails the same
// way, so the caller reads the recorded error rather than catching one.
void errorTrampoline(::RtMidiError::Type type,
                     const std::string& /*text*/,
                     void* userData)
{
    if (userData != nullptr)
        static_cast<std::atomic<Error>*>(userData)->store(toError(type));
}
} // namespace

MidiManager::MidiManager()
{
    // Constructing an RtMidi object is where the platform's MIDI client is created,
    // and on iOS that fails — so it happens under the guard like everything else and
    // leaves the manager usable but empty rather than throwing out of a constructor.
    guard([this] { inputEnumerator = EA::makeOwned<::RtMidiIn>(); });
    guard([this] { outputEnumerator = EA::makeOwned<::RtMidiOut>(); });
    guard([this] { output = EA::makeOwned<::RtMidiOut>(); });

    watch(inputEnumerator.get());
    watch(outputEnumerator.get());
    watch(output.get());
}

void MidiManager::watch(::RtMidi* midi)
{
    if (midi != nullptr)
        midi->setErrorCallback(errorTrampoline, &pendingError);
}

Error MidiManager::getLastError() const
{
    return lastError;
}

bool MidiManager::isAvailable() const
{
    return inputEnumerator != nullptr && outputEnumerator != nullptr;
}

Vector<MidiPortInfo> MidiManager::getInputPorts()
{
    if (inputEnumerator == nullptr)
        return {};

    return getPorts(*inputEnumerator);
}

Vector<MidiPortInfo> MidiManager::getOutputPorts()
{
    if (outputEnumerator == nullptr)
        return {};

    return getPorts(*outputEnumerator);
}

InputPort* MidiManager::createInput(int portId, const MidiInputCallback& cb)
{
    auto& port = inputs.createNew();
    port.portId = portId;
    port.callback = cb;

    guard([&port] { port.rtIn = EA::makeOwned<::RtMidiIn>(); });

    if (port.rtIn == nullptr)
    {
        inputs.eraseIf([portId](auto& p) { return p->portId == portId; });
        return nullptr;
    }

    watch(port.rtIn.get());
    port.rtIn->setCallback(midiInputTrampoline, &port);

    return &port;
}

Error MidiManager::openInput(int portId, const MidiInputCallback& cb)
{
    closeInput(portId);

    auto* port = createInput(portId, cb);

    if (port == nullptr)
        return lastError;

    auto error = guard([port, portId]
                       { port->rtIn->openPort(static_cast<unsigned int>(portId)); });

    if (error != Error::NoError)
        closeInput(portId);

    return error;
}

std::optional<int> MidiManager::openVirtualInput(const std::string& name,
                                                 const MidiInputCallback& cb)
{
    auto portId = nextVirtualPortId--;
    auto* port = createInput(portId, cb);

    if (port == nullptr)
        return std::nullopt;

    auto error = guard([port, &name] { port->rtIn->openVirtualPort(name); });

    if (error != Error::NoError)
    {
        closeInput(portId);
        return std::nullopt;
    }

    return portId;
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

Error MidiManager::openOutput(int portId)
{
    closeOutput();

    if (output == nullptr)
        return Error::SYSTEM_ERROR;

    return guard([this, portId]
                 { output->openPort(static_cast<unsigned int>(portId)); });
}

Error MidiManager::openVirtualOutput(const std::string& name)
{
    closeOutput();

    if (output == nullptr)
        return Error::SYSTEM_ERROR;

    return guard([this, &name] { output->openVirtualPort(name); });
}

void MidiManager::closeOutput()
{
    if (output != nullptr && output->isPortOpen())
        guard([this] { output->closePort(); });
}

bool MidiManager::isOutputOpen() const
{
    return output != nullptr && output->isPortOpen();
}

Error MidiManager::sendMessage(const std::uint8_t* bytes, std::size_t size)
{
    // Sending to nothing used to succeed silently, which reads as a dead cable.
    if (!isOutputOpen())
        return Error::INVALID_USE;

    return guard([this, bytes, size] { output->sendMessage(bytes, size); });
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
        port.scratch.timestamp = timestamp;
        port.scratch.bytes.assign(message->begin(), message->end());
        port.callback(port.scratch);
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
