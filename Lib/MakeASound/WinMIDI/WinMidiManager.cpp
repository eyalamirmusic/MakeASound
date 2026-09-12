#include "WinMidiManager.h"
#include "../MIDI/MIDI.h"

#include <chrono>
#include <cstring>
#include <utility>

namespace MakeASound::WinMIDI
{
namespace
{
constexpr auto headerSize = static_cast<UINT>(sizeof(MIDIHDR));

// How many milliseconds close() spends waiting for the driver to hand its
// buffers back. midiInReset returns them all, so this is only ever one pass.
constexpr auto closeAttempts = 100;

// SysEx is the one thing convertMidi does not carry, and the queue tier only
// takes a dump that fits an Event. Anything longer belongs in callback mode.
std::optional<MIDI::Event> toEvent(Span<const std::uint8_t> bytes)
{
    if (bytes.empty())
        return std::nullopt;

    if (bytes[0] == 0xF0)
    {
        if (bytes.size() > MIDI::SysEx::maxBytes)
            return std::nullopt;

        return MIDI::Event::sysEx(bytes.data(), bytes.size());
    }

    return MIDI::convertMidi(bytes.data(), bytes.size());
}

bool containsId(const Vector<MidiPortInfo>& ports, int id)
{
    for (const auto& port: ports)
        if (port.id == id)
            return true;

    return false;
}

void rememberIds(Vector<int>& known, const Vector<MidiPortInfo>& ports)
{
    known.clear();
    known.reserve(ports.size());

    for (const auto& port: ports)
        known.add(port.id);
}

// WinMM's own thread. Nothing here may call a multimedia function other than
// midiInAddBuffer, which is the one the ring needs.
void CALLBACK midiInputTrampoline(HMIDIIN,
                                  UINT message,
                                  DWORD_PTR instance,
                                  DWORD_PTR param1,
                                  DWORD_PTR param2)
{
    auto* port = reinterpret_cast<InputPort*>(instance);

    if (port == nullptr)
        return;

    if (message == MIM_DATA)
        port->handleShortMessage(param1, param2);
    else if (message == MIM_LONGDATA || message == MIM_LONGERROR)
        port->handleLongMessage(reinterpret_cast<MIDIHDR*>(param1),
                                param2,
                                message == MIM_LONGERROR);
}
} // namespace

InputPort::~InputPort()
{
    // WinMM first, so nothing can be in the callback for storage that is about
    // to go away with the rest of this object.
    close();
}

Error InputPort::open(int deviceNumber,
                      int bytesForSysEx,
                      MidiTimePoint epochToUse,
                      SharedState& state)
{
    shared = &state;
    epoch = epochToUse;

    sysExStorage.resize(static_cast<std::size_t>(bytesForSysEx));
    parser.setSysExBuffer(Span<std::uint8_t>(sysExStorage));

    // One mode's storage, never both. The callback's message never outgrows the
    // dump the parser can assemble, so reserving here is what keeps assign() off
    // the heap on the MIDI thread; queue mode has no use for it, and callback
    // mode none for the queue.
    if (callback)
        scratch.bytes.reserve(sysExStorage.size());
    else
        queue.create();

    auto result = midiInOpen(&handle,
                             static_cast<UINT>(deviceNumber),
                             reinterpret_cast<DWORD_PTR>(&midiInputTrampoline),
                             reinterpret_cast<DWORD_PTR>(this),
                             CALLBACK_FUNCTION);

    if (result != MMSYSERR_NOERROR)
    {
        handle = nullptr;
        return getError(result);
    }

    for (auto& buffer: buffers)
    {
        buffer.header = MIDIHDR {};
        buffer.header.lpData = buffer.storage.data();
        buffer.header.dwBufferLength = static_cast<DWORD>(SysExBuffer::bytes);

        result = midiInPrepareHeader(handle, &buffer.header, headerSize);

        if (result == MMSYSERR_NOERROR)
        {
            buffer.prepared = true;
            result = midiInAddBuffer(handle, &buffer.header, headerSize);
        }

        if (result != MMSYSERR_NOERROR)
        {
            close();
            return getError(result);
        }
    }

    // WinMM starts counting its milliseconds inside midiInStart, so this is the
    // one place the two clocks meet. Taken just before rather than just after:
    // the first message can be delivered before the call returns, and an arrival
    // a call-duration late beats one racing an unwritten clock base.
    startTime = std::chrono::steady_clock::now();

    result = midiInStart(handle);

    if (result != MMSYSERR_NOERROR)
    {
        close();
        return getError(result);
    }

    running = true;
    return Error::NoError;
}

bool InputPort::unprepareBuffers()
{
    auto allDone = true;

    for (auto& buffer: buffers)
    {
        if (!buffer.prepared)
            continue;

        if (midiInUnprepareHeader(handle, &buffer.header, headerSize)
            == MIDIERR_STILLPLAYING)
            allDone = false;
        else
            buffer.prepared = false;
    }

    return allDone;
}

void InputPort::close()
{
    if (handle == nullptr)
        return;

    closing.store(true, std::memory_order_relaxed);

    if (running)
        midiInStop(handle);

    // Reset hands every queued buffer back through the callback, and only a
    // buffer that is back can be unprepared. A callback already past its closing
    // check can re-arm one behind the reset, so this is a loop rather than a
    // single pass — midiInClose refuses a handle that still holds a buffer.
    for (auto attempt = 0; attempt < closeAttempts; ++attempt)
    {
        midiInReset(handle);

        if (unprepareBuffers())
            break;

        Sleep(1);
    }

    midiInClose(handle);

    handle = nullptr;
    running = false;
}

void InputPort::handleShortMessage(DWORD_PTR packed, DWORD_PTR timestamp)
{
    auto bytes = std::array<std::uint8_t, 3> {};
    auto length = unpackShortMessage(static_cast<DWORD>(packed), bytes);

    if (length > 0)
        feed(bytes.data(),
             length,
             toTimePoint(startTime, static_cast<DWORD>(timestamp)));
}

void InputPort::handleLongMessage(MIDIHDR* midiHeader,
                                  DWORD_PTR timestamp,
                                  bool failed)
{
    if (midiHeader == nullptr)
        return;

    auto recorded = static_cast<int>(midiHeader->dwBytesRecorded);

    // A failed dump is left to the parser's own recovery: it abandons what it
    // has when the next status byte arrives, or when the timeout runs out.
    if (!failed && recorded > 0)
        feed(reinterpret_cast<const std::uint8_t*>(midiHeader->lpData),
             recorded,
             toTimePoint(startTime, static_cast<DWORD>(timestamp)));

    // midiInReset hands every buffer back empty on the way out, and re-arming
    // one then is what wedges the machine minutes later.
    if (recorded == 0 || closing.load(std::memory_order_relaxed))
        return;

    midiInAddBuffer(handle, midiHeader, headerSize);
}

void InputPort::feed(const std::uint8_t* bytes, int size, MidiTimePoint timestamp)
{
    auto flags = shared->ignoredTypes.load(std::memory_order_relaxed);
    parser.setIgnoredTypes((flags & ignoreClockFlag) != 0,
                           (flags & ignoreActiveSenseFlag) != 0);

    auto onMessage = [this](const MidiMessageView& message) { deliver(message); };

    auto onDropped = [this](int)
    { shared->droppedSysEx.store(true, std::memory_order_relaxed); };

    parser.feed(Span<const std::uint8_t>(bytes, size),
                timestamp,
                onMessage,
                onDropped);
}

void InputPort::deliver(const MidiMessageView& message)
{
    if (callback)
    {
        scratch.timestamp =
            std::chrono::duration<double>(message.timestamp - epoch).count();

        scratch.bytes.assign(message.bytes.begin(), message.bytes.end());
        callback(scratch);
        return;
    }

    auto typed = toEvent(message.bytes);

    if (!typed)
    {
        if (!message.bytes.empty() && message.bytes[0] == 0xF0)
            shared->droppedSysEx.store(true, std::memory_order_relaxed);

        return;
    }

    auto event = MidiInputEvent {portId, *typed, message.timestamp};

    if (!queue->push(event))
        shared->overflowed.store(true, std::memory_order_relaxed);
}

MidiManager::MidiManager()
    : epoch(std::chrono::steady_clock::now())
{
    // The baseline the first diff is measured against: a manager announces what
    // changed after it came up, not what was already plugged in.
    rememberIds(knownInputs, getPorts(Direction::Input, inputRegistry));
    rememberIds(knownOutputs, getPorts(Direction::Output, outputRegistry));
}

MidiManager::~MidiManager()
{
    closeAllInputs();
    closeOutput();
}

Error MidiManager::record(MMRESULT result)
{
    return record(getError(result));
}

Error MidiManager::record(Error error)
{
    lastError.store(error, std::memory_order_relaxed);
    return error;
}

Error MidiManager::getLastError() const
{
    return lastError.load(std::memory_order_relaxed);
}

bool MidiManager::isAvailable() const
{
    // WinMM ships with every Windows this library builds for; a machine with no
    // MIDI interface simply enumerates nothing.
    return true;
}

Vector<MidiPortInfo> MidiManager::scan(Direction direction)
{
    auto isInput = direction == Direction::Input;
    auto ports = getPorts(direction, isInput ? inputRegistry : outputRegistry);

    publishPortChanges(isInput ? knownInputs : knownOutputs, ports);
    publishFlags();

    return ports;
}

Vector<MidiPortInfo> MidiManager::getInputPorts()
{
    return scan(Direction::Input);
}

Vector<MidiPortInfo> MidiManager::getOutputPorts()
{
    return scan(Direction::Output);
}

void MidiManager::publishFlags()
{
    if (shared.overflowed.exchange(false))
        notifyHost(MidiNotification::QueueOverflow);

    if (shared.droppedSysEx.exchange(false))
        notifyHost(MidiNotification::SysExDropped);
}

void MidiManager::publishPortChanges(Vector<int>& known,
                                     const Vector<MidiPortInfo>& ports)
{
    auto added = 0;
    auto removed = 0;

    for (const auto& port: ports)
        if (!known.contains(port.id))
            ++added;

    for (auto id: known)
        if (!containsId(ports, id))
            ++removed;

    // Brought up to date before anything is raised: a host that enumerates from
    // inside its own notification callback would otherwise re-enter this while
    // it was still walking `known`.
    rememberIds(known, ports);

    for (auto i = 0; i < added; ++i)
        notifyHost(MidiNotification::PortAdded);

    for (auto i = 0; i < removed; ++i)
        notifyHost(MidiNotification::PortRemoved);
}

int MidiManager::resolveInput(int portId)
{
    scan(Direction::Input);
    return inputRegistry.portNumberForId(portId);
}

int MidiManager::resolveOutput(int portId)
{
    scan(Direction::Output);
    return outputRegistry.portNumberForId(portId);
}

void MidiManager::setIgnoredTypes(bool clock, bool activeSense)
{
    MidiBackend::setIgnoredTypes(clock, activeSense);

    auto flags = static_cast<std::uint8_t>(
        (clock ? ignoreClockFlag : 0) | (activeSense ? ignoreActiveSenseFlag : 0));

    shared.ignoredTypes.store(flags, std::memory_order_relaxed);
}

Error MidiManager::openInput(int portId, const MidiInputCallback& cb)
{
    closeInput(portId);

    auto deviceNumber = resolveInput(portId);

    if (deviceNumber < 0)
        return record(Error::INVALID_DEVICE);

    // Built before the lock is taken: open() sizes the port's buffers and opens
    // the device, and the drain has better things to do than wait behind that. A
    // port that failed to open never reaches the list, so its own destructor is
    // what unwinds it.
    auto port = EA::makeOwned<InputPort>();

    port->portId = portId;
    port->callback = cb;

    auto error = port->open(deviceNumber, getMaxSysExBytes(), epoch, shared);

    if (error != Error::NoError)
        return record(error);

    auto lock = ScopedSpinLock(inputsLock);
    inputs.add(std::move(port));

    return record(Error::NoError);
}

std::optional<int> MidiManager::openVirtualInput(const std::string&,
                                                 const MidiInputCallback&)
{
    // WinMM has no virtual endpoints at all. Windows MIDI Services does, and is
    // the second Windows TU this seam exists for.
    record(Error::INVALID_USE);
    return std::nullopt;
}

void MidiManager::closeInput(int portId)
{
    auto lock = ScopedSpinLock(inputsLock);
    inputs.eraseIf([portId](auto& p) { return p->portId == portId; });
}

void MidiManager::closeAllInputs()
{
    auto lock = ScopedSpinLock(inputsLock);
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
    // Contended only while a port is opening or closing, and this is the audio
    // thread: the events keep, so the block gives up its turn instead.
    auto lock = ScopedTryLock(inputsLock);

    if (!lock.held)
        return;

    auto event = MidiInputEvent {};

    for (auto& port: inputs)
    {
        if (port->callback)
            continue;

        while (port->queue->pop(event))
            out.add(event);
    }
}

Error MidiManager::openOutput(int portId)
{
    closeOutput();

    auto deviceNumber = resolveOutput(portId);

    if (deviceNumber < 0)
        return record(Error::INVALID_DEVICE);

    auto result = midiOutOpen(&output,
                              static_cast<UINT>(deviceNumber),
                              0,
                              0,
                              CALLBACK_NULL);

    if (result != MMSYSERR_NOERROR)
    {
        output = nullptr;
        return record(result);
    }

    sysExOut.resize(static_cast<std::size_t>(getMaxSysExBytes()));

    return record(Error::NoError);
}

Error MidiManager::openVirtualOutput(const std::string&)
{
    return record(Error::INVALID_USE);
}

void MidiManager::closeOutput()
{
    if (output == nullptr)
        return;

    // No midiOutReset on the way out: it sends all-notes-off and reset-all-
    // controllers on all sixteen channels, which closing a port should not do.
    midiOutClose(output);
    output = nullptr;
}

bool MidiManager::isOutputOpen() const
{
    return output != nullptr;
}

Error MidiManager::sendMessage(const std::uint8_t* bytes, std::size_t size)
{
    // Sending to nothing used to succeed silently, which reads as a dead cable.
    if (!isOutputOpen())
        return record(Error::INVALID_USE);

    if (bytes == nullptr || size == 0)
        return record(Error::INVALID_PARAMETER);

    auto total = static_cast<int>(size);

    if (bytes[0] == 0xF0)
        return sendSysEx(bytes, total);

    if (total > 3)
        return record(Error::INVALID_PARAMETER);

    auto packed = DWORD {};

    for (auto i = 0; i < total; ++i)
        packed |= static_cast<DWORD>(bytes[i]) << (8 * i);

    return record(midiOutShortMsg(output, packed));
}

Error MidiManager::sendSysEx(const std::uint8_t* bytes, int size)
{
    // The staging buffer is the manager's SysEx ceiling going out: the send path
    // is allocation-free after open, so a longer dump is refused rather than
    // growing the heap under whichever thread called.
    if (size > static_cast<int>(sysExOut.size()))
        return record(Error::INVALID_PARAMETER);

    std::memcpy(sysExOut.data(), bytes, static_cast<std::size_t>(size));

    auto midiHeader = MIDIHDR {};
    midiHeader.lpData = sysExOut.data();
    midiHeader.dwBufferLength = static_cast<DWORD>(size);

    auto result = midiOutPrepareHeader(output, &midiHeader, headerSize);

    if (result != MMSYSERR_NOERROR)
        return record(result);

    result = midiOutLongMsg(output, &midiHeader, headerSize);

    // Unpreparing is the wait: it answers STILLPLAYING until the dump is off the
    // wire, which is what keeps sendMessage synchronous and the staging buffer
    // safe to refill. A long dump therefore blocks for as long as the port takes
    // to send it, so it is not something to do from an audio callback.
    while (midiOutUnprepareHeader(output, &midiHeader, headerSize)
           == MIDIERR_STILLPLAYING)
        Sleep(1);

    return record(result);
}

} // namespace MakeASound::WinMIDI
