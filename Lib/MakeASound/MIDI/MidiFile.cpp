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

// The largest value a 4-byte variable-length quantity holds.
constexpr auto maxDelta = std::int64_t {0x0FFFFFFF};

// 2^53: the largest tick a time is written at (every integer up to it is a
// double).
constexpr auto maxTick = 9007199254740992.0;

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

// Times in ascending order with `<`, a NaN after every number: a strict weak
// order, so sorting never sees an inconsistent comparison.
bool timeBefore(double a, double b) noexcept
{
    return !std::isnan(a) && (std::isnan(b) || a < b);
}

// A time rounded to its tick, ties to even; NaN and negative times give 0, and
// anything past 2^53 gives 2^53.
std::int64_t tickOf(double time) noexcept
{
    auto rounded = std::nearbyint(time);

    if (!(rounded > 0.0))
        return 0;

    return static_cast<std::int64_t>(std::min(rounded, maxTick));
}

// Where a stored meta event's data starts (after FF, the type and the length
// field) and whether the length field matches the data. When the length field
// is malformed or cut, the data is everything after the type byte.
struct MetaLayout
{
    std::size_t dataStart = 2;
    bool consistent = false;
};

MetaLayout metaLayout(const Bytes& bytes) noexcept
{
    auto length = std::size_t {0};
    auto position = std::size_t {2};

    for (auto i = 0; i < 4 && position < bytes.size(); ++i)
    {
        auto next = bytes[position++];
        length = (length << 7) | (next & 0x7Fu);

        if ((next & 0x80) == 0)
            return {position, position + length == bytes.size()};
    }

    return {};
}

// Whether the writer writes this event as an end-of-track: a meta event of
// type 2F whose data, once its length is re-encoded, is empty.
bool writesAsEndOfTrack(const Bytes& bytes) noexcept
{
    return bytes.size() >= 2 && bytes[0] == metaStatus && bytes[1] == endOfTrackType
           && metaLayout(bytes).dataStart == bytes.size();
}

// Reads over one chunk's bytes; every read fails rather than run past the end.
struct ChunkReader
{
    const std::uint8_t* data = nullptr;
    std::int64_t size = 0;
    std::int64_t position = 0;

    std::int64_t remaining() const noexcept { return size - position; }

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
        if (static_cast<std::int64_t>(count) > remaining())
            return false;

        auto first = data + position;
        append(bytes, first, first + count);
        position += count;
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
            || static_cast<std::int64_t>(length) > reader.remaining())
            return false;

        reader.position += length;
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

Track readTrack(const std::uint8_t* data, std::int64_t size)
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

// Writes one MTrk chunk to `out`: clamped deltas from the rounded times, running
// status, and an end-of-track when none was added.
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

    // An event that cannot be written (empty, not starting with a status byte,
    // a lone FF, or shorter than its status needs) is skipped, delta included.
    void add(const TimedMessage& message)
    {
        auto& bytes = message.bytes;

        if (bytes.empty())
            return;

        auto status = static_cast<int>(bytes[0]);

        if (status == metaStatus)
            addMeta(message);
        else if (status == sysExStatus || status == sysExEscapeStatus)
            addSysEx(message);
        else if (status >= 0x80)
            addShortMessage(message);
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
    void addDelta(double time)
    {
        auto tick = tickOf(time);
        auto delta = std::clamp(tick - previousTick, std::int64_t {0}, maxDelta);
        previousTick = tick;
        appendVlq(out, static_cast<std::uint32_t>(delta));
    }

    // Verbatim when the length field matches the data; otherwise re-encoded as
    // FF type VLQ(data size) data.
    void addMeta(const TimedMessage& message)
    {
        auto& bytes = message.bytes;

        if (bytes.size() < 2)
            return;

        auto layout = metaLayout(bytes);
        auto data = bytes.data() + layout.dataStart;
        auto last = bytes.data() + bytes.size();

        addDelta(message.time);

        if (layout.consistent)
        {
            append(out, bytes.data(), last);
        }
        else
        {
            out.push_back(bytes[0]);
            out.push_back(bytes[1]);
            appendVlq(out, static_cast<std::uint32_t>(last - data));
            append(out, data, last);
        }

        if (bytes[1] == endOfTrackType && data == last)
            ended = true;

        previousStatus = metaStatus;
    }

    void addSysEx(const TimedMessage& message)
    {
        auto& bytes = message.bytes;

        addDelta(message.time);
        out.push_back(bytes[0]);
        appendVlq(out, static_cast<std::uint32_t>(bytes.size() - 1));
        append(out, bytes.data() + 1, bytes.data() + bytes.size());
        previousStatus = bytes[0];
    }

    // A channel or system message, at exactly its status's length; a channel
    // status equal to the previous event's is omitted (running status).
    void addShortMessage(const TimedMessage& message)
    {
        auto& bytes = message.bytes;
        auto status = static_cast<int>(bytes[0]);
        auto dataBytes = dataByteCount(status);

        if (static_cast<int>(bytes.size()) - 1 < dataBytes)
            return;

        addDelta(message.time);

        auto isChannel = status <= 0xEF;
        auto first = bytes.data() + (isChannel && status == previousStatus ? 1 : 0);
        append(out, first, bytes.data() + 1 + dataBytes);

        // Every event sets it, so a meta, SysEx or system event cancels it.
        previousStatus = status;
    }

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
// end-of-track at the latest time of them all (end-of-tracks included, NaNs
// not).
void writeMergedTrack(Bytes& out, const std::vector<Track>& tracks)
{
    auto merged = std::vector<const TimedMessage*> {};
    auto endTime = 0.0;
    auto first = true;

    for (auto& track: tracks)
    {
        for (auto& message: track.events)
        {
            if (!std::isnan(message.time) && (first || endTime < message.time))
            {
                endTime = message.time;
                first = false;
            }

            if (!writesAsEndOfTrack(message.bytes))
                merged.push_back(&message);
        }
    }

    std::stable_sort(merged.begin(),
                     merged.end(),
                     [](const TimedMessage* a, const TimedMessage* b)
                     { return timeBefore(a->time, b->time); });

    auto writer = TrackWriter {out};

    for (auto message: merged)
        writer.add(*message);

    writer.add(TimedMessage::endOfTrack(endTime));
    writer.finish();
}

// The PPQ seconds arithmetic, shared by TempoMap::ticksToSeconds and
// File::tempoMap so the two give the same bits. 120 bpm until the first tempo;
// every entry (time signatures too) splits the accumulation; both multiply-adds
// are fused (one rounding each), written with std::fma rather than left to
// contraction.
struct TempoWalk
{
    explicit TempoWalk(int ticksPerQuarterNote) noexcept
        : tickLength(1.0 / ticksPerQuarterNote)
        , secondsPerTick(0.5 * tickLength)
    {
    }

    double secondsAt(double ticks) const noexcept
    {
        return std::fma(ticks - lastTick, secondsPerTick, accumulated);
    }

    void pass(const TimedMessage& message) noexcept
    {
        accumulated = std::fma(message.time - lastTick, secondsPerTick, accumulated);
        lastTick = message.time;

        if (message.isTempo())
        {
            auto microseconds = message.microsecondsPerQuarterNote();
            secondsPerTick = tickLength * (microseconds / 1000000.0);
        }
    }

    double tickLength;
    double secondsPerTick;
    double accumulated = 0.0;
    double lastTick = 0.0;
};
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
        // SMPTE: code 29 is 30 drop-frame, 30000/1001 frames per second; the
        // other codes are literal, so their product is exact.
        auto frameCode = -(timeFormat >> 8);
        auto ticksPerFrame = timeFormat & 0xFF;
        auto rate =
            frameCode == 29 ? 30000.0 / 1001.0 : static_cast<double>(frameCode);
        return ticks / (rate * static_cast<double>(ticksPerFrame));
    }

    auto walk = TempoWalk {timeFormat};

    // Stops at the first entry not before `ticks` (a NaN entry included).
    for (auto& event: events)
    {
        if (!(event.message.time < ticks))
            break;

        walk.pass(event.message);
    }

    return walk.secondsAt(ticks);
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
    auto isSmpte = (division & 0x8000) != 0;

    if (format > 2 || division == 0 || (isSmpte && (division & 0xFF) == 0))
        return false;

    auto readTracks = std::vector<Track> {};
    auto position = chunkHeaderSize + headerLength;

    // Other chunks are skipped and not counted; a chunk's length is clamped to
    // the bytes remaining; whatever follows the last counted track is ignored.
    while (static_cast<int>(readTracks.size()) < declaredTracks
           && size - position >= chunkHeaderSize)
    {
        auto chunk = bytes + position;
        auto declaredLength = static_cast<std::int64_t>(readBE32(chunk + 4));
        position += chunkHeaderSize;

        auto length = std::min(declaredLength, size - position);

        if (idIs(chunk, trackId))
            readTracks.push_back(readTrack(bytes + position, length));

        position += length;
    }

    timeFormat = static_cast<int>(static_cast<std::int16_t>(division));
    fileType = format;
    tracks = std::move(readTracks);
    return true;
}

bool File::writeTo(std::vector<std::uint8_t>& out, int type) const
{
    if (type == -1)
        type = fileType;

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
    auto& events = map.events;

    for (auto& track: tracks)
        for (auto& message: track.events)
            if (message.isTempo())
                events.push_back({message, 0.0});

    for (auto& track: tracks)
        for (auto& message: track.events)
            if (message.isTimeSignature())
                events.push_back({message, 0.0});

    std::stable_sort(events.begin(),
                     events.end(),
                     [](const TempoEvent& a, const TempoEvent& b)
                     { return timeBefore(a.message.time, b.message.time); });

    if (timeFormat <= 0)
    {
        for (auto& event: events)
            event.seconds = map.ticksToSeconds(event.message.time);

        return map;
    }

    // One pass, the same bits as map.ticksToSeconds(time) for each entry: the
    // entries before a time are a prefix of the sorted map, so each group of
    // equal times reads the walk so far, then is folded into it.
    auto walk = TempoWalk {timeFormat};
    auto count = events.size();

    for (auto start = std::size_t {0}; start < count;)
    {
        auto time = events[start].message.time;
        auto seconds = walk.secondsAt(time);
        auto end = start;

        while (end < count && (end == start || events[end].message.time == time))
            events[end++].seconds = seconds;

        for (auto i = start; i < end; ++i)
            walk.pass(events[i].message);

        start = end;
    }

    return map;
}

double File::ticksToSeconds(double ticks) const
{
    return tempoMap().ticksToSeconds(ticks);
}

} // namespace MakeASound::MIDI
