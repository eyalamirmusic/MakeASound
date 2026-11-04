#include "MidiFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <utility>

namespace MakeASound::MIDI
{
namespace
{
using Bytes = std::vector<std::uint8_t>;

constexpr auto metaStatus = 0xFF;
constexpr auto sysExStatus = 0xF0;
constexpr auto sysExEscapeStatus = 0xF7;
constexpr auto endOfTrackType = 0x2F;
constexpr auto tempoType = 0x51;
constexpr auto timeSignatureType = 0x58;

constexpr auto headerId = "MThd";
constexpr auto trackId = "MTrk";
constexpr auto chunkHeaderSize = 8;
constexpr auto headerDataSize = 6;

std::uint8_t byte(std::uint32_t value) noexcept
{
    return static_cast<std::uint8_t>(value & 0xFF);
}

void append(Bytes& out, const std::uint8_t* first, const std::uint8_t* last)
{
    out.insert(out.end(), first, last);
}

void appendId(Bytes& out, const char* id)
{
    for (auto i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>(id[i]));
}

void appendBE16(Bytes& out, std::uint32_t value)
{
    out.push_back(byte(value >> 8));
    out.push_back(byte(value));
}

void appendBE32(Bytes& out, std::uint32_t value)
{
    out.push_back(byte(value >> 24));
    out.push_back(byte(value >> 16));
    out.push_back(byte(value >> 8));
    out.push_back(byte(value));
}

// The shortest form: 7-bit groups, most significant first, every byte but the
// last with bit 7 set.
void appendVlq(Bytes& out, std::uint32_t value)
{
    std::uint8_t groups[5] = {};
    auto count = 0;

    do
    {
        groups[count++] = byte(value & 0x7F);
        value >>= 7;
    } while (value > 0);

    for (auto i = count - 1; i >= 0; --i)
        out.push_back(i > 0 ? byte(groups[i] | 0x80u) : groups[i]);
}

std::uint32_t readBE16(const std::uint8_t* bytes) noexcept
{
    return (std::uint32_t(bytes[0]) << 8) | std::uint32_t(bytes[1]);
}

std::uint32_t readBE32(const std::uint8_t* bytes) noexcept
{
    return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16)
           | (std::uint32_t(bytes[2]) << 8) | std::uint32_t(bytes[3]);
}

bool idIs(const std::uint8_t* bytes, const char* id) noexcept
{
    return std::memcmp(bytes, id, 4) == 0;
}

// The data bytes that follow a channel or system status byte.
int dataByteCount(int status) noexcept
{
    if (status < 0xF0)
    {
        auto kind = status & 0xF0;
        return kind == 0xC0 || kind == 0xD0 ? 1 : 2;
    }

    if (status == 0xF1 || status == 0xF3)
        return 1;

    if (status == 0xF2)
        return 2;

    return 0;
}

// Reads over one chunk's bytes; every read fails rather than run past the end.
struct ChunkReader
{
    const std::uint8_t* data = nullptr;
    int size = 0;
    int position = 0;

    int remaining() const noexcept { return size - position; }

    // False when the data ends before the last byte, or when a fourth byte
    // still has bit 7 set.
    bool readVlq(std::uint32_t& value) noexcept
    {
        value = 0;

        for (auto i = 0; i < 4; ++i)
        {
            if (remaining() <= 0)
                return false;

            auto next = data[position++];
            value = (value << 7) | (next & 0x7Fu);

            if ((next & 0x80) == 0)
                return true;
        }

        return false;
    }

    // Appends the next `count` bytes to `bytes`, or fails when fewer remain.
    bool take(Bytes& bytes, std::uint32_t count)
    {
        if (count > static_cast<std::uint32_t>(remaining()))
            return false;

        auto first = data + position;
        append(bytes, first, first + count);
        position += static_cast<int>(count);
        return true;
    }
};

// One event at the reader's position, in its stored form. False when it runs
// past the chunk or needs a running status that does not exist.
bool readEvent(ChunkReader& reader, int& runningStatus, Bytes& bytes)
{
    auto status = static_cast<int>(reader.data[reader.position]);

    if (status == metaStatus)
    {
        auto start = reader.position;
        reader.position += 1;

        if (reader.remaining() <= 0)
            return false;

        reader.position += 1;
        auto length = std::uint32_t {0};

        if (!reader.readVlq(length)
            || length > static_cast<std::uint32_t>(reader.remaining()))
            return false;

        reader.position += static_cast<int>(length);
        append(bytes, reader.data + start, reader.data + reader.position);
        return true;
    }

    if (status == sysExStatus || status == sysExEscapeStatus)
    {
        reader.position += 1;
        auto length = std::uint32_t {0};

        if (!reader.readVlq(length))
            return false;

        bytes.push_back(static_cast<std::uint8_t>(status));
        return reader.take(bytes, length);
    }

    if (status >= 0x80)
    {
        reader.position += 1;

        // Meta, SysEx and system events leave the running status alone.
        if (status < 0xF0)
            runningStatus = status;
    }
    else if (runningStatus == 0)
    {
        return false;
    }
    else
    {
        status = runningStatus;
    }

    bytes.push_back(static_cast<std::uint8_t>(status));
    return reader.take(bytes, static_cast<std::uint32_t>(dataByteCount(status)));
}

Track readTrack(const std::uint8_t* data, int size)
{
    auto track = Track {};
    auto reader = ChunkReader {data, size};
    auto tick = std::int64_t {0};
    auto runningStatus = 0;

    while (reader.remaining() > 0)
    {
        auto delta = std::uint32_t {0};

        if (!reader.readVlq(delta))
            break;

        tick += delta;

        if (reader.remaining() <= 0)
            break;

        auto message = TimedMessage {};
        message.time = static_cast<double>(tick);

        if (!readEvent(reader, runningStatus, message.bytes))
            break;

        auto ended = message.isEndOfTrack();
        track.events.push_back(std::move(message));

        if (ended)
            break;
    }

    return track;
}

// Writes one MTrk chunk to `out`: deltas from the rounded times, running status,
// and an end-of-track when none was added.
class TrackWriter
{
public:
    explicit TrackWriter(Bytes& outToUse)
        : out(outToUse)
    {
        appendId(out, trackId);
        lengthAt = out.size();
        appendBE32(out, 0);
    }

    void add(const TimedMessage& message)
    {
        auto& bytes = message.bytes;

        if (bytes.empty())
            return;

        auto tick = static_cast<std::int64_t>(std::nearbyint(message.time));
        auto delta = std::max(std::int64_t {0}, tick - previousTick);
        previousTick = tick;
        appendVlq(out, static_cast<std::uint32_t>(delta));

        auto status = static_cast<int>(bytes[0]);
        auto isChannel = status >= 0x80 && status <= 0xEF;
        auto rest = bytes.data() + 1;
        auto last = bytes.data() + bytes.size();

        if (isChannel && status == previousStatus && bytes.size() > 1)
        {
            append(out, rest, last);
        }
        else if (status == sysExStatus || status == sysExEscapeStatus)
        {
            out.push_back(bytes[0]);
            appendVlq(out, static_cast<std::uint32_t>(bytes.size() - 1));
            append(out, rest, last);
        }
        else
        {
            append(out, bytes.data(), last);
        }

        // Every event sets it, so a meta, SysEx or system event cancels it.
        previousStatus = status;

        if (message.isEndOfTrack())
            ended = true;
    }

    void finish()
    {
        if (!ended)
        {
            const std::uint8_t endOfTrack[] = {
                0x00, metaStatus, endOfTrackType, 0x00};
            append(out, std::begin(endOfTrack), std::end(endOfTrack));
        }

        auto length = static_cast<std::uint32_t>(out.size() - lengthAt - 4);
        out[lengthAt] = byte(length >> 24);
        out[lengthAt + 1] = byte(length >> 16);
        out[lengthAt + 2] = byte(length >> 8);
        out[lengthAt + 3] = byte(length);
    }

private:
    Bytes& out;
    std::size_t lengthAt = 0;
    std::int64_t previousTick = 0;
    int previousStatus = -1;
    bool ended = false;
};

void writeTrack(Bytes& out, const Track& track)
{
    auto writer = TrackWriter {out};

    for (auto& message: track.events)
        writer.add(message);

    writer.finish();
}

// Every track's events but the end-of-tracks, stably sorted by time, then one
// end-of-track at the latest time of them all (end-of-tracks included).
void writeMergedTrack(Bytes& out, const std::vector<Track>& tracks)
{
    auto merged = std::vector<const TimedMessage*> {};
    auto endTime = 0.0;
    auto first = true;

    for (auto& track: tracks)
    {
        for (auto& message: track.events)
        {
            if (first || endTime < message.time)
                endTime = message.time;

            first = false;

            if (!message.isEndOfTrack())
                merged.push_back(&message);
        }
    }

    std::stable_sort(merged.begin(),
                     merged.end(),
                     [](const TimedMessage* a, const TimedMessage* b)
                     { return a->time < b->time; });

    auto writer = TrackWriter {out};

    for (auto message: merged)
        writer.add(*message);

    writer.add(TimedMessage::endOfTrack(endTime));
    writer.finish();
}
} // namespace

bool TimedMessage::isMeta() const noexcept
{
    return bytes.size() >= 2 && bytes[0] == metaStatus;
}

int TimedMessage::metaType() const noexcept
{
    return isMeta() ? static_cast<int>(bytes[1]) : -1;
}

bool TimedMessage::isEndOfTrack() const noexcept
{
    return isMeta() && bytes[1] == endOfTrackType;
}

bool TimedMessage::isTempo() const noexcept
{
    return bytes.size() >= 6 && bytes[0] == metaStatus && bytes[1] == tempoType
           && bytes[2] == 3;
}

bool TimedMessage::isTimeSignature() const noexcept
{
    return bytes.size() >= 7 && bytes[0] == metaStatus
           && bytes[1] == timeSignatureType && bytes[2] == 4;
}

bool TimedMessage::isSysEx() const noexcept
{
    return !bytes.empty()
           && (bytes[0] == sysExStatus || bytes[0] == sysExEscapeStatus);
}

int TimedMessage::microsecondsPerQuarterNote() const noexcept
{
    if (!isTempo())
        return 0;

    return (static_cast<int>(bytes[3]) << 16) | (static_cast<int>(bytes[4]) << 8)
           | static_cast<int>(bytes[5]);
}

int TimedMessage::timeSignatureNumerator() const noexcept
{
    return isTimeSignature() ? static_cast<int>(bytes[3]) : 0;
}

int TimedMessage::timeSignatureDenominator() const noexcept
{
    if (!isTimeSignature() || bytes[4] > 30)
        return 0;

    return 1 << bytes[4];
}

TimedMessage TimedMessage::meta(double time, int type, Span<const std::uint8_t> data)
{
    auto message = TimedMessage {};
    message.time = time;
    message.bytes.push_back(metaStatus);
    message.bytes.push_back(static_cast<std::uint8_t>(type));
    appendVlq(message.bytes, static_cast<std::uint32_t>(data.getSize()));
    append(message.bytes, data.begin(), data.end());
    return message;
}

TimedMessage TimedMessage::endOfTrack(double time)
{
    return meta(time, endOfTrackType);
}

TimedMessage TimedMessage::tempo(double time, int microsecondsPerQuarterNote)
{
    auto value = static_cast<std::uint32_t>(microsecondsPerQuarterNote);
    const std::uint8_t data[] = {byte(value >> 16), byte(value >> 8), byte(value)};
    return meta(time, tempoType, Span<const std::uint8_t>(data));
}

TimedMessage TimedMessage::timeSignature(double time,
                                         int numerator,
                                         int denominator,
                                         int clocksPerClick,
                                         int thirtySecondsPerQuarter)
{
    auto power = 0;

    while (power < 30 && (1 << power) < denominator)
        ++power;

    const std::uint8_t data[] = {static_cast<std::uint8_t>(numerator),
                                 static_cast<std::uint8_t>(power),
                                 static_cast<std::uint8_t>(clocksPerClick),
                                 static_cast<std::uint8_t>(thirtySecondsPerQuarter)};
    return meta(time, timeSignatureType, Span<const std::uint8_t>(data));
}

double TempoMap::ticksToSeconds(double ticks) const noexcept
{
    if (timeFormat == 0)
        return ticks;

    if (timeFormat < 0)
    {
        // SMPTE: code 29 is 30 drop-frame, 29.97 frames per second; the other
        // codes are literal, so their product is exact.
        auto frameCode = -(timeFormat >> 8);
        auto ticksPerFrame = timeFormat & 0xFF;
        auto rate = frameCode == 29 ? 29.97 : static_cast<double>(frameCode);
        return ticks / (rate * static_cast<double>(ticksPerFrame));
    }

    // Both multiply-adds are fused (one rounding each): the pinned values depend
    // on it, so they are written with std::fma rather than left to contraction.
    auto tickLength = 1.0 / timeFormat;
    auto secondsPerTick = 0.5 * tickLength;
    auto accumulated = 0.0;
    auto lastTick = 0.0;

    for (auto& event: events)
    {
        auto tick = event.message.time;

        if (tick >= ticks)
            break;

        accumulated = std::fma(tick - lastTick, secondsPerTick, accumulated);
        lastTick = tick;

        if (event.message.isTempo())
        {
            auto microseconds = event.message.microsecondsPerQuarterNote();
            secondsPerTick = tickLength * (microseconds / 1000000.0);
        }
    }

    return std::fma(ticks - lastTick, secondsPerTick, accumulated);
}

int smpteTimeFormat(int framesPerSecond, int ticksPerFrame) noexcept
{
    return (-framesPerSecond * 256) | ticksPerFrame;
}

bool File::readFrom(Span<const std::uint8_t> data)
{
    auto bytes = data.data();
    auto size = static_cast<std::int64_t>(data.getSize());

    if (size < chunkHeaderSize + headerDataSize || !idIs(bytes, headerId))
        return false;

    auto headerLength = static_cast<std::int64_t>(readBE32(bytes + 4));

    if (headerLength < headerDataSize || chunkHeaderSize + headerLength > size)
        return false;

    auto format = static_cast<int>(readBE16(bytes + 8));
    auto declaredTracks = static_cast<int>(readBE16(bytes + 10));
    auto division = readBE16(bytes + 12);

    if (format > 2 || division == 0)
        return false;

    auto readTracks = std::vector<Track> {};
    auto position = chunkHeaderSize + headerLength;

    // Other chunks are skipped and not counted; a chunk whose length runs past
    // the end is read as far as the data goes; whatever follows the last
    // counted track is ignored.
    while (static_cast<int>(readTracks.size()) < declaredTracks
           && size - position >= chunkHeaderSize)
    {
        auto chunk = bytes + position;
        auto declaredLength = static_cast<std::int64_t>(readBE32(chunk + 4));
        position += chunkHeaderSize;

        auto length = std::min(declaredLength, size - position);

        if (idIs(chunk, trackId))
            readTracks.push_back(
                readTrack(bytes + position, static_cast<int>(length)));

        position += length;
    }

    timeFormat = static_cast<int>(static_cast<std::int16_t>(division));
    fileType = format;
    tracks = std::move(readTracks);
    return true;
}

bool File::writeTo(std::vector<std::uint8_t>& out, int type) const
{
    if (type < 0 || type > 2)
        return false;

    if (timeFormat == 0 || timeFormat < -32768 || timeFormat > 32767)
        return false;

    auto merge = type == 0 && tracks.size() != 1;

    if (!merge && tracks.size() > 65535)
        return false;

    auto trackCount = merge ? 1u : static_cast<std::uint32_t>(tracks.size());

    appendId(out, headerId);
    appendBE32(out, headerDataSize);
    appendBE16(out, static_cast<std::uint32_t>(type));
    appendBE16(out, trackCount);
    appendBE16(out, static_cast<std::uint32_t>(timeFormat) & 0xFFFF);

    if (merge)
    {
        writeMergedTrack(out, tracks);
        return true;
    }

    for (auto& track: tracks)
        writeTrack(out, track);

    return true;
}

std::vector<std::uint8_t> File::toBytes(int type) const
{
    auto out = std::vector<std::uint8_t> {};
    writeTo(out, type);
    return out;
}

TempoMap File::tempoMap() const
{
    auto map = TempoMap {};
    map.timeFormat = timeFormat;

    for (auto& track: tracks)
        for (auto& message: track.events)
            if (message.isTempo())
                map.events.push_back({message, 0.0});

    for (auto& track: tracks)
        for (auto& message: track.events)
            if (message.isTimeSignature())
                map.events.push_back({message, 0.0});

    std::stable_sort(map.events.begin(),
                     map.events.end(),
                     [](const TempoEvent& a, const TempoEvent& b)
                     { return a.message.time < b.message.time; });

    for (auto& event: map.events)
        event.seconds = map.ticksToSeconds(event.message.time);

    return map;
}

double File::ticksToSeconds(double ticks) const
{
    return tempoMap().ticksToSeconds(ticks);
}

} // namespace MakeASound::MIDI
