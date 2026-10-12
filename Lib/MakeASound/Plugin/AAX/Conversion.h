#pragma once

#include "AAXCommon.h"
#include "../../Audio/Playhead.h"
#include "../../MIDI/MIDI.h"
#include "../../MIDI/MidiParser.h"

#include <optional>

namespace MakeASound::AAX
{

// Pro Tools' tick resolution.
constexpr auto ticksPerQuarter = 960000.0;

// What the transport says at the start of the block; isValid when any call
// answered.
Playhead readPlayhead(const AAX_ITransport& transport) noexcept;

// The playhead `samples` further on at the same tempo, for a block's later slices.
Playhead advance(Playhead playhead, int samples, double sampleRate) noexcept;

// The node's packets through the parser, each message at the timestamp of the
// packet that completed it, clamped into the block; a SysEx split across packets
// is assembled. A full buffer drops the event.
void readMidi(const AAX_CMidiStream& stream,
              MidiParser& parser,
              MidiTimePoint now,
              int numSamples,
              MIDI::Buffer& into) noexcept;

// Channel-voice messages only, since Pro Tools routes no plugin SysEx.
std::optional<AAX_CMidiPacket> packetFor(const MIDI::Event& event,
                                         int sampleOffset) noexcept;

} // namespace MakeASound::AAX
