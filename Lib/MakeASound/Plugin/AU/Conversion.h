#pragma once

#include "AUCommon.h"
#include "../../Audio/Playhead.h"
#include "../../MIDI/MIDI.h"

#include <CoreMIDI/CoreMIDI.h>

#include <optional>

namespace MakeASound::AU
{

// What the host's callbacks say; the sample time is the transport's timeline
// position, or the timestamp's stream clock when the host has no transport.
// isValid when any callback answered.
Playhead readPlayhead(const au::AUBase& unit,
                      const AudioTimeStamp& timestamp) noexcept;

float from7Bit(UInt8 value) noexcept;

// 14-bit, centred at 8192, to -1..+1.
float fromPitchWheel(UInt8 lsb, UInt8 msb) noexcept;

// A whole F0..F7 message; nullopt past MIDI::SysEx::maxBytes.
std::optional<MIDI::Event> sysExEvent(const UInt8* data, UInt32 length) noexcept;

// Appends the event as one packet stamped with its sample offset. Null when the
// list is full, as MIDIPacketListAdd answers; an event with no bytes adds nothing.
MIDIPacket* addPacket(MIDIPacketList& list,
                      ByteCount capacity,
                      MIDIPacket* current,
                      const MIDI::Event& event) noexcept;

} // namespace MakeASound::AU
