#pragma once

#include "../../Audio/Playhead.h"
#include "../../MIDI/MIDI.h"

#include <clap/events.h>

namespace MakeASound::CLAP
{

// A note, choke, MIDI or SysEx event as a MIDI 1.0 event at its time, with the
// note port it names; false for any other event or one with no MIDI meaning (a
// wildcard key, a SysEx past MIDI::SysEx::maxBytes). Note ids are ignored, a
// wildcard channel is channel 1, a wildcard port is -1, and a choke is a note
// off. `time` is the event's own, already clamped into the block.
bool fromClap(const clap_event_header& header,
              int time,
              MIDI::Event& out,
              int& port) noexcept;

// The block's transport. Without seconds the sample position stays 0.
Playhead toPlayhead(const clap_event_transport& transport,
                    double sampleRate) noexcept;

// Out events, each at `time`: a MIDI event as CLAP_EVENT_MIDI, a SysEx as
// CLAP_EVENT_MIDI_SYSEX pointing into `event` (the host copies it during the
// push); a parameter value; a gesture's begin or end. False when the host's
// list refused it.
bool pushMidi(const clap_output_events& out,
              const MIDI::Event& event,
              int port,
              uint32_t time) noexcept;
bool pushParamValue(const clap_output_events& out,
                    clap_id id,
                    double value,
                    uint32_t time = 0) noexcept;
bool pushGesture(const clap_output_events& out,
                 clap_id id,
                 bool begin,
                 uint32_t time = 0) noexcept;

} // namespace MakeASound::CLAP
