#include "WinMidiManager.h"

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
void CALLBACK midiInputTrampoline(
    HMIDIIN, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2)
{
    auto* port = reinterpret_cast<InputPort*>(instance);

    if (port == nullptr)
        return;

    if (message == MIM_DATA)
        port->handleShortMessage(param1, param2);
    else if (message == MIM_LONGDATA || message == MIM_LONGERROR)
        port->handleLongMessage(
            reinterpret_cast<MIDIHDR*>(param1), param2, message == MIM_LONGERROR);
}
} // namespace

InputPort::~InputPort()
{
    // WinMM first, so nothing can be in the callback for storage that is about
    // to go away with the rest of this object.
    close();
}

Error InputPort::open(int deviceNumber)
{
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
        pipe.feed(Span<const std::uint8_t>(bytes.data(), length),
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
    {
        auto* data = reinterpret_cast<const std::uint8_t*>(midiHeader->lpData);

        pipe.feed(Span<const std::uint8_t>(data, recorded),
                  toTimePoint(startTime, static_cast<DWORD>(timestamp)));
    }

    // midiInReset hands every buffer back empty on the way out, and re-arming
    // one then is what wedges the machine minutes later.
    if (recorded == 0 || closing.load(std::memory_order_relaxed))
        return;

    midiInAddBuffer(handle, midiHeader, headerSize);
}

MidiManager::MidiManager()
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
    publishInputFlags();

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

Error MidiManager::openInput(int portId, const MidiInputCallback& cb)
{
    closeInput(portId);

    auto deviceNumber = resolveInput(portId);

    if (deviceNumber < 0)
        return record(Error::INVALID_DEVICE);

    // Built before the lock is taken: the pipe sizes its buffers and open()
    // opens the device, and the drain has better things to do than wait behind
    // that. A port that failed to open never reaches the list, so its own
    // destructor is what unwinds it.
    auto port = makeInput(portId, cb);
    auto error = port->open(deviceNumber);

    if (error != Error::NoError)
        return record(error);

    addInput(std::move(port));

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

Error MidiManager::openOutput(int portId)
{
    closeOutput();

    auto deviceNumber = resolveOutput(portId);

    if (deviceNumber < 0)
        return record(Error::INVALID_DEVICE);

    auto result =
        midiOutOpen(&output, static_cast<UINT>(deviceNumber), 0, 0, CALLBACK_NULL);

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
