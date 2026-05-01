#pragma once

#include "../Common/Common.h"
#include "../MIDI/MIDI.h"
#include <Miro/Miro.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MakeASound
{

// Wall-clock timestamp used to align MIDI arrival with audio block timing.
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

    double timestamp {};
    std::vector<std::uint8_t> bytes;
};

// Queue entry used by the audio-thread MIDI path. Carries a typed
// MIDI::Event (no heap) plus the bookkeeping needed to assign a sample
// offset on drain. The event itself owns its sampleOffset, which
// MidiBlockSync writes after wall-clock-to-sample translation.
struct MidiInputEvent
{
    int portId {};
    MIDI::Event event;

    // Set by the input backend at the moment RtMidi delivered the message.
    // Read by MidiBlockSync to translate to a sample offset.
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

using MidiInputCallback = std::function<void(const MidiMessage&)>;

// Render a MIDI message as a human-readable line: decoded status (Note On,
// CC, Pitch Bend, ...), channel (1-based) and data bytes, followed by a
// hex dump of the raw payload. Intended for logs, debug UIs and demos.
std::string formatMessage(const MidiMessage& message);

} // namespace MakeASound
