#pragma once

#include "../Common/Common.h"
#include "MidiInfo.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace MakeASound
{

// A complete message, as a view into storage the parser owns. Valid only for
// the duration of the sink call — copy what you keep.
struct MidiMessageView
{
    Span<const std::uint8_t> bytes;
    MidiTimePoint timestamp {};
};

// A non-owning call target: two pointers, no heap, no std::function. Binds to
// any callable for the duration of the feed() call it is passed to; a
// default-constructed one is a no-op.
template <class... Args>
class MidiSink
{
public:
    MidiSink() = default;

    template <class Fn>
        requires(!std::is_same_v<std::remove_cvref_t<Fn>, MidiSink>)
    MidiSink(Fn&& fn) noexcept
        : target(const_cast<void*>(static_cast<const void*>(std::addressof(fn))))
        , invoke(
              [](void* self, Args... args)
              {
                  using Target = std::remove_reference_t<Fn>;
                  (*static_cast<Target*>(self))(args...);
              })
    {
    }

    void operator()(Args... args) const
    {
        if (invoke != nullptr)
            invoke(target, args...);
    }

private:
    void* target = nullptr;
    void (*invoke)(void*, Args...) = nullptr;
};

using MidiMessageSink = MidiSink<const MidiMessageView&>;

// Byte count of the whole dump as it was seen, including the leading 0xF0 and
// the terminating 0xF7 when one arrived.
using MidiSysExDropSink = MidiSink<int>;

// Platform-agnostic MIDI 1.0 byte-stream parser: running status, interleaved
// realtime bytes, and SysEx assembled into a buffer the owner hands in. Pure —
// no heap, no locks, no logging, no clock of its own — so every backend can
// run it straight on the platform's MIDI thread.
//
// A message that spans feeds is stamped with the timestamp of the feed it
// started in; realtime bytes are stamped with the feed they arrived in.
class MidiParser
{
public:
    using Milliseconds = std::chrono::milliseconds;

    static constexpr auto defaultSysExTimeout = Milliseconds {1000};

    MidiParser() = default;
    explicit MidiParser(Span<std::uint8_t> buffer) noexcept;

    // The assembly buffer for SysEx. Anything longer is dropped, so size it for
    // the largest dump the port should accept. Resets any dump in flight.
    void setSysExBuffer(Span<std::uint8_t> buffer) noexcept;

    // Parses `bytes`, calling `onMessage` once per complete message and
    // `onSysExDropped` once per dump that overflowed the buffer or was
    // abandoned. Feeding nothing still runs the timeout, so an owner with a
    // stalled dump can tick the parser from its own thread.
    void feed(Span<const std::uint8_t> bytes,
              MidiTimePoint timestamp,
              MidiMessageSink onMessage,
              MidiSysExDropSink onSysExDropped = {}) noexcept;

    // Forgets running status and anything half-parsed, silently.
    void reset() noexcept;

    // Both are filtered by default, matching what the MIDI input side has
    // always delivered.
    void setIgnoredTypes(bool clock, bool activeSense) noexcept;

    // How long a dump may stall before it is abandoned, measured against the
    // timestamps fed in rather than any clock of the parser's own.
    void setSysExTimeout(Milliseconds timeout) noexcept;

    bool isInSysEx() const noexcept { return inSysEx; }

private:
    void parse(std::uint8_t byte,
               MidiTimePoint timestamp,
               const MidiMessageSink& onMessage,
               const MidiSysExDropSink& onSysExDropped) noexcept;

    void beginSysEx(MidiTimePoint timestamp) noexcept;
    void appendSysEx(std::uint8_t byte) noexcept;
    void endSysEx(const MidiMessageSink& onMessage,
                  const MidiSysExDropSink& onSysExDropped) noexcept;
    void dropSysEx(const MidiSysExDropSink& onSysExDropped) noexcept;

    static void emit(const std::uint8_t* data,
                     int size,
                     MidiTimePoint timestamp,
                     const MidiMessageSink& onMessage) noexcept;

    Span<std::uint8_t> sysExBuffer;
    Milliseconds sysExTimeout = defaultSysExTimeout;

    std::array<std::uint8_t, 3> pending {};
    int pendingLength = 0;
    int pendingExpected = 0;
    MidiTimePoint pendingTime {};

    std::uint8_t runningStatus = 0;

    bool inSysEx = false;
    bool sysExOverflowed = false;
    int sysExLength = 0;
    int sysExSeen = 0;
    MidiTimePoint sysExStartTime {};
    MidiTimePoint sysExActivity {};

    bool ignoreClock = true;
    bool ignoreActiveSense = true;
};

} // namespace MakeASound
