#pragma once

#include "../Common/Common.h"
#include "MIDI.h"
#include <Miro/Miro.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MakeASound
{

using MidiTimePoint = std::chrono::steady_clock::time_point;

struct MidiPortInfo
{
    MIRO_REFLECT(id, name)
    bool operator==(const MidiPortInfo&) const = default;

    int id {};
    std::string name;
};

struct MidiMessage
{
    MIRO_REFLECT(timestamp, bytes)

    // Seconds since the MidiManager was created, taken from the packet's own
    // hardware stamp on MidiManager::now()'s clock — so two messages can be
    // subtracted for the interval between them, at the platform's resolution
    // rather than the delivery thread's.
    double timestamp {};

    // One whole message, framing bytes included: a SysEx arrives with its 0xF0
    // and 0xF7. The buffer belongs to the port and the next message refills it,
    // so copy what you keep.
    std::vector<std::uint8_t> bytes;
};

struct MidiInputEvent
{
    int portId {};
    MIDI::Event event;

    // Stamped on MidiManager::now()'s clock when the message arrived;
    // MidiBlockSync translates it into event.sampleOffset.
    MidiTimePoint arrival {};
};

class MidiEvents
{
public:
    static constexpr int defaultCapacity = 1024;

    MidiEvents() { events.reserve(defaultCapacity); }
    explicit MidiEvents(int capacity) { events.reserve(capacity); }

    void clear() noexcept { events.clear(); }
    bool empty() const noexcept { return events.empty(); }
    int size() const noexcept { return events.size(); }

    auto begin() noexcept { return events.begin(); }
    auto end() noexcept { return events.end(); }
    auto begin() const noexcept { return events.begin(); }
    auto end() const noexcept { return events.end(); }

    MidiInputEvent& operator[](int i) { return events[i]; }
    const MidiInputEvent& operator[](int i) const { return events[i]; }

    Vector<MidiInputEvent>& raw() noexcept { return events; }
    const Vector<MidiInputEvent>& raw() const noexcept { return events; }

private:
    Vector<MidiInputEvent> events;
};

// Something the platform's MIDI system did on its own, or a limit this library hit
// on the way through. Informational only.
enum class MidiNotification
{
    PortAdded,
    PortRemoved,
    SysExDropped,
    QueueOverflow
};

using MidiInputCallback = std::function<void(const MidiMessage&)>;
using MidiNotificationCallback = std::function<void(MidiNotification)>;

// Decoded status, data bytes and a hex dump; channel is rendered 1-based.
std::string formatMessage(const MidiMessage& message);

} // namespace MakeASound
