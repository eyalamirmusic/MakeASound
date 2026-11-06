#pragma once

#include "../Common/Common.h"

#include <cstdint>
#include <vector>

/** @file
    Standard MIDI Files (SMF 0, 1 and 2): reading bytes into tracks of timed
    events and writing them back. The events are kept as raw bytes, in file
    order, with absolute times in ticks; nothing is sorted, paired or dropped on
    the way in or out. Policies a caller may want on top (ordering of
    simultaneous events, note-on/note-off pairing, whole-file conversion to
    seconds) are not this type's job. Not realtime: everything here may allocate.
*/

namespace MakeASound::MIDI
{

/// One event of a track. `time` is in ticks. `bytes` is the complete event in its
/// stored form:
///   channel message     8n..En with its data bytes, always with the status byte
///   meta event          FF type VLQ(length) data..., exactly as in the file
///   SysEx               F0 followed by the bytes after the file's length field
///                       (normally ending in F7); F7 escapes likewise
///   system message      F1..F6, F8..FE with their data bytes
/// Each event owns its bytes in a heap vector, so a file's memory grows with its
/// event count; reading has no cap on the number of events.
struct TimedMessage
{
    double time = 0.0;
    std::vector<std::uint8_t> bytes;

    bool operator==(const TimedMessage&) const = default;

    bool isMeta() const noexcept;
    /// The meta type byte, or -1 when this is not a meta event.
    int metaType() const noexcept;
    bool isEndOfTrack() const noexcept;
    /// FF 51 03 tt tt tt
    bool isTempo() const noexcept;
    /// FF 58 04 nn dd cc bb
    bool isTimeSignature() const noexcept;
    /// F0 or F7
    bool isSysEx() const noexcept;

    /// 0 unless isTempo().
    int microsecondsPerQuarterNote() const noexcept;
    /// 0 unless isTimeSignature(); the denominator is 2^dd.
    int timeSignatureNumerator() const noexcept;
    int timeSignatureDenominator() const noexcept;

    /// FF type VLQ(data.size()) data...
    static TimedMessage
        meta(double time, int type, Span<const std::uint8_t> data = {});
    /// FF 2F 00
    static TimedMessage endOfTrack(double time);
    /// FF 51 03 and the three bytes of `microsecondsPerQuarterNote`.
    static TimedMessage tempo(double time, int microsecondsPerQuarterNote);
    /// FF 58 04 numerator log2(denominator) clocksPerClick thirtySecondsPerQuarter;
    /// `denominator` is a power of two.
    static TimedMessage timeSignature(double time,
                                      int numerator,
                                      int denominator,
                                      int clocksPerClick = 24,
                                      int thirtySecondsPerQuarter = 8);
};

struct Track
{
    std::vector<TimedMessage> events;

    bool operator==(const Track&) const = default;
};

/// A tempo or time-signature event of a file, with its time in ticks (in
/// `message.time`) and in seconds.
struct TempoEvent
{
    TimedMessage message;
    double seconds = 0.0;

    bool operator==(const TempoEvent&) const = default;
};

/// Every tempo and time-signature event of a file, ordered by tick (stable: the
/// tempos before the time signatures at one tick, each in track order).
struct TempoMap
{
    int timeFormat = 480;
    std::vector<TempoEvent> events;

    bool operator==(const TempoMap&) const = default;

    /// Ticks to seconds over `events` (120 bpm before the first tempo); SMPTE
    /// formats ignore the tempo and divide by frames per second times ticks per
    /// frame; a time format of 0 returns `ticks` unchanged.
    double ticksToSeconds(double ticks) const noexcept;
};

/// The time format of an SMPTE division: (-framesPerSecond << 8) | ticksPerFrame,
/// so smpteTimeFormat(25, 40) == -6360 (the header's E7 28).
int smpteTimeFormat(int framesPerSecond, int ticksPerFrame) noexcept;

struct File
{
    /// Positive: ticks per quarter note. Negative: SMPTE (see smpteTimeFormat).
    /// The header's division as a signed 16-bit value.
    int timeFormat = 480;

    /// The header's format (0, 1 or 2) as last read. writeTo ignores it and
    /// takes its own `type`.
    int fileType = 1;

    std::vector<Track> tracks;

    bool operator==(const File&) const = default;

    /// Reads a whole file. False (and the File untouched) when the header is not
    /// usable (including a division of 0, or SMPTE with 0 ticks per frame);
    /// otherwise true, with every track chunk the header counts, each parsed as
    /// far as its bytes go. A chunk length is clamped to the bytes remaining, so
    /// a length past the end (up to FFFFFFFF) reads to the end of the data. Bytes
    /// after the last counted track are ignored.
    bool readFrom(Span<const std::uint8_t> data);

    /// Appends the file to `out`: header, then one chunk per track with running
    /// status, events in the order given. `type` -1 (the default) means
    /// `fileType`. Type 0 writes a single track, merging several by time
    /// (stable). Times are rounded to ticks (NaN and negatives to 0, at most 2^53)
    /// and each delta is clamped to 0FFFFFFF. A meta event whose length field
    /// disagrees with its bytes is re-encoded; a channel or system message is
    /// written at its status's length (extra bytes dropped); an event too short
    /// for its status, or empty, or not starting with a status byte, is skipped.
    /// Only an end-of-track with no data counts as the track's end. False, and
    /// nothing appended, for a
    /// type other than 0, 1 or 2, a time format of 0 or outside 16 bits, or too
    /// many tracks.
    bool writeTo(std::vector<std::uint8_t>& out, int type = -1) const;

    /// What writeTo would append, or an empty vector when it would fail.
    std::vector<std::uint8_t> toBytes(int type = -1) const;

    TempoMap tempoMap() const;

    /// tempoMap().ticksToSeconds(ticks). This builds the tempo map on every call:
    /// to convert many times, build tempoMap() once and use its ticksToSeconds.
    double ticksToSeconds(double ticks) const;
};

} // namespace MakeASound::MIDI
