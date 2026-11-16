#pragma once

#include "../../Audio/BusLayout.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>

namespace MakeASound::CLAP
{

// The audio ports a layout declares, each side's ids its bus indices: the main
// bus flagged CLAP_AUDIO_PORT_IS_MAIN, a mono or stereo bus typed so, and the
// main input and output an in-place pair where their widths match. 32-bit only.
int audioPortCount(const BusLayout& layout, bool isInput) noexcept;
bool audioPortInfo(const BusLayout& layout,
                   int index,
                   bool isInput,
                   clap_audio_port_info& info) noexcept;

// The note ports, one per MIDI bus, ids their indices: CLAP and MIDI dialects,
// MIDI preferred, which is what the plugin sends.
int notePortCount(const BusLayout& layout, bool isInput) noexcept;
bool notePortInfo(const BusLayout& layout,
                  int index,
                  bool isInput,
                  clap_note_port_info& info) noexcept;

// clap.audio-ports and clap.note-ports, over the Adapter behind the plugin.
const clap_plugin_audio_ports* audioPortsExtension() noexcept;
const clap_plugin_note_ports* notePortsExtension() noexcept;

} // namespace MakeASound::CLAP
