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

    unsigned int id {};
    std::string name;
};

struct MidiMessage
{
    MIRO_REFLECT(timestamp, bytes)

    double timestamp {};
    std::vector<std::uint8_t> bytes;
};

using MidiInputCallback = std::function<void(const MidiMessage&)>;

} // namespace MakeASound
