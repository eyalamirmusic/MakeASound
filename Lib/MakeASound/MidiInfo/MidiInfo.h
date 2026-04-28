#pragma once

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

    unsigned int id {};
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

    unsigned int portId {};
    MidiMessage message;
};

class MidiEvents
{
public:
    static constexpr std::size_t defaultCapacity = 1024;

    MidiEvents() { events.reserve(defaultCapacity); }
    explicit MidiEvents(std::size_t capacity) { events.reserve(capacity); }

    void clear() noexcept { events.clear(); }
    bool empty() const noexcept { return events.empty(); }
    std::size_t size() const noexcept { return events.size(); }

    auto begin() noexcept { return events.begin(); }
    auto end() noexcept { return events.end(); }
    auto begin() const noexcept { return events.begin(); }
    auto end() const noexcept { return events.end(); }

    MidiInputEvent& operator[](std::size_t i) { return events[i]; }
    const MidiInputEvent& operator[](std::size_t i) const { return events[i]; }

    std::vector<MidiInputEvent>& raw() noexcept { return events; }
    const std::vector<MidiInputEvent>& raw() const noexcept { return events; }

private:
    std::vector<MidiInputEvent> events;
};

using MidiInputCallback = std::function<void(const MidiMessage&)>;

} // namespace MakeASound
