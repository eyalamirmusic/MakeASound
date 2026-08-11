#pragma once

#include "../Common/Common.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace MakeASound::MIDI
{

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

// Capped because the realtime stream only carries short control messages
// (GM/GS/XG reset, MTC, master volume); longer dumps need a non-realtime path.
struct SysEx
{
    static constexpr int maxBytes = 16;

    std::array<uint8_t, maxBytes> data {};
    int size = 0;
};

template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

class Event
{
public:
    int sampleOffset = 0;

    // 0..15 for voice/channel messages; -1 for SysEx and anything else
    // where a MIDI channel is not meaningful.
    int channel = 0;

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

    // Asserts in debug and yields an empty SysEx if `bytes` is null or
    // `size` is out of [0, SysEx::maxBytes].
    static Event
        sysEx(const uint8_t* bytes, int size, int sampleOffset = 0) noexcept;

    bool isNoteOn() const noexcept;
    bool isNoteOff() const noexcept;
    bool isControlChange() const noexcept;
    bool isPitchBend() const noexcept;
    bool isChannelAftertouch() const noexcept;
    bool isPolyAftertouch() const noexcept;
    bool isProgramChange() const noexcept;
    bool isSysEx() const noexcept;

    // Null unless the event is of that kind (std::get_if semantics).
    const NoteOn* asNoteOn() const noexcept;
    const NoteOff* asNoteOff() const noexcept;
    const ControlChange* asControlChange() const noexcept;
    const PitchBend* asPitchBend() const noexcept;
    const ChannelAftertouch* asChannelAftertouch() const noexcept;
    const PolyAftertouch* asPolyAftertouch() const noexcept;
    const ProgramChange* asProgramChange() const noexcept;
    const SysEx* asSysEx() const noexcept;

    template <class Visitor>
    decltype(auto) visit(Visitor&& vis) const
    {
        return std::visit(std::forward<Visitor>(vis), payload);
    }

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

    // Stable (ties keep insertion order, so a note-off still precedes a
    // note-on at the same offset) and allocation-free: audio-thread safe.
    void sortByOffset() noexcept;
};

// nullopt for messages that don't map (SysEx, MTC, song-position, undersized
// payloads). Allocation-free — callable from any real-time thread.
std::optional<Event> convertMidi(const std::uint8_t* bytes,
                                 int size,
                                 int sampleOffset = 0) noexcept;

struct RawBytes
{
    static constexpr int maxBytes = SysEx::maxBytes;

    std::array<std::uint8_t, maxBytes> data {};
    int size = 0;

    const std::uint8_t* begin() const noexcept { return data.data(); }
    const std::uint8_t* end() const noexcept { return data.data() + size; }
};

// Inverse of convertMidi: normalized floats are quantized to 7-bit / 14-bit
// and clamped to range. Allocation-free.
RawBytes toBytes(const Event& event) noexcept;

std::string toString(const Event& event);

} // namespace MakeASound::MIDI
