#include "RTMidi-Backend.h"

namespace MakeASound::RTMidi
{

Vector<MidiPortInfo> getPorts(::RtMidi& backend, MidiPortRegistry& registry)
{
    auto result = Vector<MidiPortInfo> {};
    auto count = backend.getPortCount();

    result.reserve(static_cast<int>(count));
    registry.beginScan();

    // The name is the only identity this backend offers; the registry is what keeps
    // two ports wearing the same one in separate slots.
    for (auto port = 0u; port < count; ++port)
    {
        auto name = backend.getPortName(port);
        auto id = registry.idFor(name, static_cast<int>(port));

        result.add(MidiPortInfo {id, name});
    }

    return result;
}

} // namespace MakeASound::RTMidi
