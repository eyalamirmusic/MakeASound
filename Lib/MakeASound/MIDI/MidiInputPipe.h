#pragma once

#include "../Common/Common.h"
#include "../Realtime/SPSCQueue.h"
#include "MidiInfo.h"
#include "MidiParser.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace MakeASound
{

// The only place a backend's MIDI thread and the host's meet. notifyHost takes a
// lock, which that thread may not, so it raises a flag here and the backend
// sweeps it into a notification from a thread that may (each backend's own: a
// run loop timer, the enumeration, a sweeper thread). The mask goes the other
// way, read on every feed so a setIgnoredTypes() reaches ports already open.
struct MidiInputFlags
{
    static constexpr auto ignoreClock = std::uint8_t {1};
    static constexpr auto ignoreActiveSense = std::uint8_t {2};

    void setIgnoredTypes(bool clock, bool activeSense) noexcept;

    std::atomic<bool> overflowed {false};
    std::atomic<bool> droppedSysEx {false};
    std::atomic<std::uint8_t> ignoredTypes {ignoreClock | ignoreActiveSense};
};

// Everything one open input does with its bytes once the platform has handed
// them over: the parser that turns them into whole messages, and the two ways a
// message leaves - straight to a callback on the platform's MIDI thread, or into
// the queue drainMessages() empties on the host's. Each backend's input port
// holds one beside its native handles.
//
// Everything the MIDI thread touches is sized in open(), and only for the mode
// the port is in: queue mode gets the queue, callback mode a scratch message as
// large as the SysEx cap, and neither pays for the other. feed() and drainInto()
// never reach the heap.
class MidiInputPipe
{
public:
    static constexpr auto queueCapacity = 2048;

    using Queue = SPSCQueue<MidiInputEvent, queueCapacity>;

    // An empty callback is queue mode. Allocates, so it runs before the port is
    // where the MIDI thread can reach it.
    void open(int portIdToUse,
              const MidiInputCallback& callbackToUse,
              int sysExBytes,
              MidiTimePoint epochToUse,
              MidiInputFlags& flagsToUse);

    // Applies the mask the host last set, then parses. Feeding nothing still runs
    // the SysEx timeout, which is how a backend ticks a dump nothing is adding to.
    void feed(Span<const std::uint8_t> bytes, MidiTimePoint timestamp) noexcept;

    // feed() in two halves, for a backend handed several chunks at once (Core
    // MIDI's packet list): the mask is read once for the lot, then each parsed.
    void loadIgnoredTypes() noexcept;
    void parse(Span<const std::uint8_t> bytes, MidiTimePoint timestamp) noexcept;

    // Queue mode's events, appended in order; a callback port has none. The host
    // side of the queue, so one thread at a time.
    void drainInto(Vector<MidiInputEvent>& out) noexcept;

    int getPortId() const noexcept { return portId; }

private:
    void deliver(const MidiMessageView& message) noexcept;

    int portId {};
    MidiInputCallback callback;

    MidiParser parser;
    std::vector<std::uint8_t> sysExStorage;
    MidiMessage scratch;

    // MidiMessage::timestamp is seconds from here, on MidiManager::now()'s clock.
    MidiTimePoint epoch {};
    MidiInputFlags* flags {};

    // Queue mode only; null where `callback` took the other path.
    OwningPointer<Queue> queue;
};

} // namespace MakeASound
