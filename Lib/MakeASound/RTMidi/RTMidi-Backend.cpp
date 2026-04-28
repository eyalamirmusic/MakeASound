#include "RTMidi-Backend.h"

namespace MakeASound::RTMidi
{

std::vector<MidiPortInfo> getPorts(::RtMidi& backend)
{
    auto result = std::vector<MidiPortInfo> {};
    auto count = backend.getPortCount();

    result.reserve(count);

    for (auto id = 0u; id < count; ++id)
        result.emplace_back(MidiPortInfo {id, backend.getPortName(id)});

    return result;
}

MidiMessage getMessage(double timestamp, const std::vector<unsigned char>& bytes)
{
    auto message = MidiMessage {};
    message.timestamp = timestamp;
    message.bytes.assign(bytes.begin(), bytes.end());
    return message;
}

} // namespace MakeASound::RTMidi
