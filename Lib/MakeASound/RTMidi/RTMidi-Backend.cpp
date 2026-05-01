#include "RTMidi-Backend.h"

namespace MakeASound::RTMidi
{

Vector<MidiPortInfo> getPorts(::RtMidi& backend)
{
    auto result = Vector<MidiPortInfo> {};
    auto count = backend.getPortCount();

    result.reserve(static_cast<int>(count));

    for (auto id = 0u; id < count; ++id)
        result.add(MidiPortInfo {static_cast<int>(id), backend.getPortName(id)});

    return result;
}

} // namespace MakeASound::RTMidi
