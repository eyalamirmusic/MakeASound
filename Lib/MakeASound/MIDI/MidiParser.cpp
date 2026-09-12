#include "MidiParser.h"

namespace MakeASound
{

namespace
{
constexpr auto statusStart = std::uint8_t {0x80};
constexpr auto systemStart = std::uint8_t {0xF0};
constexpr auto realtimeStart = std::uint8_t {0xF8};

constexpr auto sysExStart = std::uint8_t {0xF0};
constexpr auto sysExEnd = std::uint8_t {0xF7};
constexpr auto timingClock = std::uint8_t {0xF8};
constexpr auto activeSensing = std::uint8_t {0xFE};

// 0xF4/0xF5 are undefined system common, 0xF9/0xFD undefined realtime. They
// carry no data bytes, so dropping one cannot desync the stream.
bool isUndefined(std::uint8_t status) noexcept
{
    return status == 0xF4 || status == 0xF5 || status == 0xF9 || status == 0xFD;
}

// Total length including the status byte; 0 for SysEx and the undefined ones.
int messageLength(std::uint8_t status) noexcept
{
    if (status < systemStart)
    {
        auto kind = static_cast<std::uint8_t>(status & 0xF0);
        return (kind == 0xC0 || kind == 0xD0) ? 2 : 3;
    }

    switch (status)
    {
        case 0xF1: // MTC quarter frame
            return 2;
        case 0xF2: // song position
            return 3;
        case 0xF3: // song select
            return 2;
        case 0xF6: // tune request
            return 1;
        default:
            return 0;
    }
}
} // namespace

MidiParser::MidiParser(Span<std::uint8_t> buffer) noexcept
    : sysExBuffer(buffer)
{
}

void MidiParser::setSysExBuffer(Span<std::uint8_t> buffer) noexcept
{
    sysExBuffer = buffer;
    reset();
}

void MidiParser::reset() noexcept
{
    pendingLength = 0;
    pendingExpected = 0;
    runningStatus = 0;
    inSysEx = false;
    sysExOverflowed = false;
    sysExLength = 0;
    sysExSeen = 0;
}

void MidiParser::setIgnoredTypes(bool clock, bool activeSense) noexcept
{
    ignoreClock = clock;
    ignoreActiveSense = activeSense;
}

void MidiParser::setSysExTimeout(Milliseconds timeout) noexcept
{
    sysExTimeout = timeout;
}

void MidiParser::feed(Span<const std::uint8_t> bytes,
                      MidiTimePoint timestamp,
                      MidiMessageSink onMessage,
                      MidiSysExDropSink onSysExDropped) noexcept
{
    if (inSysEx && timestamp - sysExActivity > sysExTimeout)
        dropSysEx(onSysExDropped);

    for (auto byte: bytes)
        parse(byte, timestamp, onMessage, onSysExDropped);
}

void MidiParser::parse(std::uint8_t byte,
                       MidiTimePoint timestamp,
                       const MidiMessageSink& onMessage,
                       const MidiSysExDropSink& onSysExDropped) noexcept
{
    // Realtime bytes may sit anywhere, including between the bytes of another
    // message or inside a dump, and leave whatever is in flight untouched.
    if (byte >= realtimeStart)
    {
        if (isUndefined(byte))
            return;

        if ((byte == timingClock && ignoreClock)
            || (byte == activeSensing && ignoreActiveSense))
            return;

        emit(&byte, 1, timestamp, onMessage);
        return;
    }

    if (byte >= statusStart)
    {
        if (inSysEx)
        {
            if (byte == sysExEnd)
            {
                endSysEx(onMessage, onSysExDropped);
                return;
            }

            // Unterminated: abandon it, then let the status byte through.
            dropSysEx(onSysExDropped);
        }

        pendingLength = 0;

        if (byte == sysExStart)
        {
            runningStatus = 0;
            beginSysEx(timestamp);
            return;
        }

        // A stray 0xF7 or an undefined status: nothing to emit, but system
        // bytes still clear running status.
        if (byte == sysExEnd || isUndefined(byte))
        {
            runningStatus = 0;
            return;
        }

        runningStatus = byte < systemStart ? byte : std::uint8_t {0};

        auto length = messageLength(byte);

        if (length == 1)
        {
            emit(&byte, 1, timestamp, onMessage);
            return;
        }

        pending[0] = byte;
        pendingLength = 1;
        pendingExpected = length;
        pendingTime = timestamp;
        return;
    }

    if (inSysEx)
    {
        appendSysEx(byte);
        sysExActivity = timestamp;
        return;
    }

    if (pendingLength == 0)
    {
        if (runningStatus == 0)
            return; // a data byte with nothing to belong to

        pending[0] = runningStatus;
        pendingLength = 1;
        pendingExpected = messageLength(runningStatus);
        pendingTime = timestamp;
    }

    pending[pendingLength++] = byte;

    if (pendingLength == pendingExpected)
    {
        emit(pending.data(), pendingLength, pendingTime, onMessage);
        pendingLength = 0;
    }
}

void MidiParser::beginSysEx(MidiTimePoint timestamp) noexcept
{
    inSysEx = true;
    sysExOverflowed = false;
    sysExLength = 0;
    sysExSeen = 0;
    sysExStartTime = timestamp;
    sysExActivity = timestamp;

    appendSysEx(sysExStart);
}

void MidiParser::appendSysEx(std::uint8_t byte) noexcept
{
    ++sysExSeen;

    if (sysExLength < sysExBuffer.size())
        sysExBuffer[sysExLength++] = byte;
    else
        sysExOverflowed = true;
}

void MidiParser::endSysEx(const MidiMessageSink& onMessage,
                          const MidiSysExDropSink& onSysExDropped) noexcept
{
    appendSysEx(sysExEnd);
    inSysEx = false;

    if (sysExOverflowed)
        onSysExDropped(sysExSeen);
    else
        emit(sysExBuffer.data(), sysExLength, sysExStartTime, onMessage);
}

void MidiParser::dropSysEx(const MidiSysExDropSink& onSysExDropped) noexcept
{
    inSysEx = false;
    onSysExDropped(sysExSeen);
}

void MidiParser::emit(const std::uint8_t* data,
                      int size,
                      MidiTimePoint timestamp,
                      const MidiMessageSink& onMessage) noexcept
{
    onMessage(MidiMessageView {Span<const std::uint8_t>(data, size), timestamp});
}

} // namespace MakeASound
