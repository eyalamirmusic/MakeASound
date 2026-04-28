#pragma once

#include "../Common/Common.h"
#include <Miro/Miro.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MakeASound
{

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

struct MidiInputEvent
{
    MIRO_REFLECT(portId, message)

    int portId {};
    MidiMessage message;
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

} // namespace MakeASound
