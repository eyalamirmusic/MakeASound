#pragma once

#include <RtMidi.h>
#include "../Common/Common.h"
#include "../MIDI/MidiInfo.h"
#include "../MIDI/MidiPortRegistry.h"
#include "../Devices/DeviceInfo.h"

namespace MakeASound::RTMidi
{

Vector<MidiPortInfo> getPorts(::RtMidi& backend, MidiPortRegistry& registry);

} // namespace MakeASound::RTMidi
