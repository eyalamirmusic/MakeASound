#pragma once

#include "../Common/Common.h"
#include "../MidiInfo/MidiInfo.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace MakeASound::MIDI
{

// --- Message types -----------------------------------------------------
//
// Each struct is one MIDI 1.0 voice/channel message or a short SysEx.
// Fields are named, typed, and ranged in plugin-friendly units — float
// values are normalized (0..1 unless noted), pitch / controller /
// program are 0..127 ints.

struct NoteOn
{
    int pitch = 0; // 0..127
    float velocity = 0.f; // 0..1
};

struct NoteOff
{
    int pitch = 0;
    float velocity = 0.f;
};

struct ControlChange
{
    int controller = 0; // 0..127
    float value = 0.f; // 0..1
};

struct PitchBend
{
    float value = 0.f; // -1..+1
};

struct ChannelAftertouch
{
    float pressure = 0.f; // 0..1
};

struct PolyAftertouch
{
    int pitch = 0;
    float pressure = 0.f;
};

struct ProgramChange
{
    int program = 0; // 0..127
};

// Fixed-capacity SysEx payload. The on-thread event stream is only meant
// to carry short control messages (GM/GS/XG reset, MTC, master volume,
// device identity, manufacturer parameter changes — all ≤ ~15 bytes).
// Anything longer should use a non-realtime mechanism instead.
struct SysEx
{
    static constexpr int maxBytes = 16;

    std::array<uint8_t, maxBytes> data {};
    int size = 0;
};

// Visitor helper for `event.visit(MIDI::overloaded { ... })`.
template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// --- Event -------------------------------------------------------------
//
// Tagged-union event. Construct via the named factories (`Event::noteOn`
// et al.), query via `isX()`, read typed data via `asX()` (returns a
// const pointer, null if the event is a different kind), or pattern-match
// with `visit(MIDI::overloaded { ... })`.
class Event
{
public:
    int sampleOffset = 0;

    // Channel in [0, 15] for voice/channel messages. `-1` for SysEx or
    // any event where a MIDI channel is not meaningful.
    int channel = 0;

    // --- Named constructors -------------------------------------------

    static Event noteOn(int channel,
                        int pitch,
                        float velocity,
                        int sampleOffset = 0) noexcept;

    static Event noteOff(int channel,
                         int pitch,
                         float velocity,
                         int sampleOffset = 0) noexcept;

    static Event controlChange(int channel,
                               int controller,
                               float value,
                               int sampleOffset = 0) noexcept;

    static Event pitchBend(int channel, float value, int sampleOffset = 0) noexcept;

    static Event channelAftertouch(int channel,
                                   float pressure,
                                   int sampleOffset = 0) noexcept;

    static Event polyAftertouch(int channel,
                                int pitch,
                                float pressure,
                                int sampleOffset = 0) noexcept;

    static Event
        programChange(int channel, int program, int sampleOffset = 0) noexcept;

    // Copies up to `SysEx::maxBytes` from `bytes`. If `size` is greater
    // than the capacity (or `bytes` is null / size is negative), asserts
    // in debug and returns an empty SysEx event (size = 0).
    static Event
        sysEx(const uint8_t* bytes, int size, int sampleOffset = 0) noexcept;

    // --- Queries ------------------------------------------------------

    bool isNoteOn() const noexcept;
    bool isNoteOff() const noexcept;
    bool isControlChange() const noexcept;
    bool isPitchBend() const noexcept;
    bool isChannelAftertouch() const noexcept;
    bool isPolyAftertouch() const noexcept;
    bool isProgramChange() const noexcept;
    bool isSysEx() const noexcept;

    // --- Typed accessors ----------------------------------------------
    //
    // Each returns a pointer into the event's payload if the event is of
    // that kind, or nullptr otherwise — matching std::get_if semantics.

    const NoteOn* asNoteOn() const noexcept;
    const NoteOff* asNoteOff() const noexcept;
    const ControlChange* asControlChange() const noexcept;
    const PitchBend* asPitchBend() const noexcept;
    const ChannelAftertouch* asChannelAftertouch() const noexcept;
    const PolyAftertouch* asPolyAftertouch() const noexcept;
    const ProgramChange* asProgramChange() const noexcept;
    const SysEx* asSysEx() const noexcept;

    // --- Visit --------------------------------------------------------

    template <class Visitor>
    decltype(auto) visit(Visitor&& vis) const
    {
        return std::visit(std::forward<Visitor>(vis), payload);
    }

    // Sorts events by time; stable insertion sort preserves insertion
    // order for ties, which keeps musical intent (e.g. note-off before
    // note-on at the same offset).
    bool operator<(const Event& other) const noexcept
    {
        return sampleOffset < other.sampleOffset;
    }

private:
    using Payload = std::variant<NoteOn,
                                 NoteOff,
                                 ControlChange,
                                 PitchBend,
                                 ChannelAftertouch,
                                 PolyAftertouch,
                                 ProgramChange,
                                 SysEx>;

    Payload payload = NoteOn {};
};

struct Buffer : Vector<Event>
{
    void addFrom(const Buffer& other) noexcept;

    // Stable in-place sort by sampleOffset. Allocation-free (insertion
    // sort) so it's safe to call from the audio thread; MIDI buffers are
    // small per block, so the O(N^2) worst case is a non-issue.
    void sortByOffset() noexcept;
};

// Translates a raw MakeASound MIDI message into a typed event. Returns
// nullopt for messages that don't map (SysEx, MTC, song-position,
// undersized payloads, etc.). Pure / lock-free / allocation-free —
// callable from the audio thread.
std::optional<Event> convertMidi(const MidiMessage& msg,
                                 int sampleOffset = 0) noexcept;

// Human-readable rendering for logging / debug.
std::string toString(const Event& event);

} // namespace MakeASound::MIDI
