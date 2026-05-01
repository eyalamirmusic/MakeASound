#pragma once

#include <RtMidi.h>
#include "../Common/Common.h"
#include "../MidiInfo/MidiInfo.h"

namespace MakeASound::RTMidi
{

Vector<MidiPortInfo> getPorts(::RtMidi& backend);

} // namespace MakeASound::RTMidi
