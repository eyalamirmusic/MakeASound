#pragma once

#include <RtMidi.h>
#include "../Common/Common.h"
#include "../MidiInfo/MidiInfo.h"

namespace MakeASound::RTMidi
{

Vector<MidiPortInfo> getPorts(::RtMidi& backend);
MidiMessage getMessage(double timestamp, const std::vector<unsigned char>& bytes);

} // namespace MakeASound::RTMidi
