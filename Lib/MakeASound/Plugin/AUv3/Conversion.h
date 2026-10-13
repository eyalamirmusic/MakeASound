#pragma once

// Objective-C++ only: the render block's conversions, each called on the audio
// thread with no allocation and no Objective-C message.

#include "../../Audio/Playhead.h"
#include "../../MIDI/MIDI.h"

#import <AudioToolbox/AudioToolbox.h>

#include <optional>

namespace MakeASound::AUv3
{

// An event's offset into the block starting at `timestamp`, clamped into the
// block: one scheduled before the block lands at 0, and AUEventSampleTimeImmediate
// plus n at n.
int sampleOffset(AUEventSampleTime time,
                 const AudioTimeStamp* timestamp,
                 AUAudioFrameCount frames) noexcept;

// A short message, or a whole F0..F7 message up to MIDI::SysEx::maxBytes; nullopt
// for what does not map.
std::optional<MIDI::Event> midiEvent(const AUMIDIEvent& event, int offset) noexcept;

// What the host's blocks say; the sample time is the transport's position, or the
// timestamp's when the host has no transport block. isValid when either answered.
Playhead readPlayhead(__unsafe_unretained AUHostMusicalContextBlock musicalContext,
                      __unsafe_unretained AUHostTransportStateBlock transportState,
                      const AudioTimeStamp* timestamp) noexcept;

} // namespace MakeASound::AUv3
