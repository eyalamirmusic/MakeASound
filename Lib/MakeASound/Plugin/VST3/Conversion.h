#pragma once

#include "VST3Common.h"
#include "HostParameters.h"
#include "../../Audio/Playhead.h"
#include "../../MIDI/MIDI.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"

namespace MakeASound::VST3
{

Playhead toPlayhead(const Vst::ProcessContext& context) noexcept;

// False for an event with no MIDI 1.0 meaning here.
bool fromVst3(const Vst::Event& in, MIDI::Event& out) noexcept;

// A SysEx's bytes point into `in`, which must outlive `out`.
bool toVst3(const MIDI::Event& in, int bus, Vst::Event& out) noexcept;

MIDI::Event
    shadowEvent(const ShadowTarget& target, int sampleOffset, float value) noexcept;

Vst::SpeakerArrangement arrangementFor(int numChannels) noexcept;

} // namespace MakeASound::VST3
