#include "Ports.h"
#include "Adapter.h"

#include <algorithm>

namespace MakeASound::CLAP
{

namespace
{
const char* portTypeFor(int numChannels) noexcept
{
    if (numChannels == 1)
        return CLAP_PORT_MONO;

    if (numChannels == 2)
        return CLAP_PORT_STEREO;

    return nullptr;
}

uint32_t audioCount(const clap_plugin* plugin, bool isInput) noexcept
{
    return static_cast<uint32_t>(
        audioPortCount(Adapter::from(plugin)->wrapper().busLayout(), isInput));
}

bool audioGet(const clap_plugin* plugin,
              uint32_t index,
              bool isInput,
              clap_audio_port_info* info) noexcept
{
    return info != nullptr
           && audioPortInfo(Adapter::from(plugin)->wrapper().busLayout(),
                            static_cast<int>(index),
                            isInput,
                            *info);
}

uint32_t noteCount(const clap_plugin* plugin, bool isInput) noexcept
{
    return static_cast<uint32_t>(
        notePortCount(Adapter::from(plugin)->wrapper().busLayout(), isInput));
}

bool noteGet(const clap_plugin* plugin,
             uint32_t index,
             bool isInput,
             clap_note_port_info* info) noexcept
{
    return info != nullptr
           && notePortInfo(Adapter::from(plugin)->wrapper().busLayout(),
                           static_cast<int>(index),
                           isInput,
                           *info);
}

constexpr auto audioPorts = clap_plugin_audio_ports {audioCount, audioGet};
constexpr auto notePorts = clap_plugin_note_ports {noteCount, noteGet};
} // namespace

int audioPortCount(const BusLayout& layout, bool isInput) noexcept
{
    return isInput ? layout.inputs.size() : layout.outputs.size();
}

bool audioPortInfo(const BusLayout& layout,
                   int index,
                   bool isInput,
                   clap_audio_port_info& info) noexcept
{
    const auto& buses = isInput ? layout.inputs : layout.outputs;

    if (index < 0 || index >= buses.size())
        return false;

    const auto& bus = buses[index];
    const auto& other = isInput ? layout.outputs : layout.inputs;

    info = {};
    info.id = static_cast<clap_id>(index);
    copyText(bus.name, info.name, CLAP_NAME_SIZE);
    info.flags = bus.isMain ? uint32_t {CLAP_AUDIO_PORT_IS_MAIN} : uint32_t {0};
    info.channel_count = static_cast<uint32_t>(std::max(0, bus.numChannels));
    info.port_type = portTypeFor(bus.numChannels);
    info.in_place_pair = CLAP_INVALID_ID;

    if (index == 0 && bus.isMain && !other.empty() && other[0].isMain
        && other[0].numChannels == bus.numChannels)
        info.in_place_pair = 0;

    return true;
}

int notePortCount(const BusLayout& layout, bool isInput) noexcept
{
    return isInput ? layout.midiInputs.size() : layout.midiOutputs.size();
}

bool notePortInfo(const BusLayout& layout,
                  int index,
                  bool isInput,
                  clap_note_port_info& info) noexcept
{
    const auto& buses = isInput ? layout.midiInputs : layout.midiOutputs;

    if (index < 0 || index >= buses.size())
        return false;

    info = {};
    info.id = static_cast<clap_id>(index);
    info.supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info.preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    copyText(buses[index].name, info.name, CLAP_NAME_SIZE);
    return true;
}

const clap_plugin_audio_ports* audioPortsExtension() noexcept
{
    return &audioPorts;
}

const clap_plugin_note_ports* notePortsExtension() noexcept
{
    return &notePorts;
}

} // namespace MakeASound::CLAP
