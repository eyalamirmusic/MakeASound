#pragma once

#include <RtMidi.h>
#include "../Common/Common.h"
#include "../MIDI/MidiInfo.h"
#include "../Devices/DeviceInfo.h"

namespace MakeASound::RTMidi
{

Vector<MidiPortInfo> getPorts(::RtMidi& backend);

// Creates the platform MIDI client RtMidi will use and hands it over, so RtMidi
// never creates one itself: on Apple it does that inside a throw() function, and
// a MIDI server it cannot reach terminates the process instead of throwing.
// False means there is no MIDI system to talk to. One TU per platform.
bool prepareMidiClient();

} // namespace MakeASound::RTMidi
