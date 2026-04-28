#pragma once

#include <RtMidi.h>
#include "../MidiInfo/MidiInfo.h"

namespace MakeASound::RTMidi
{

std::vector<MidiPortInfo> getPorts(::RtMidi& backend);
MidiMessage getMessage(double timestamp, const std::vector<unsigned char>& bytes);

} // namespace MakeASound::RTMidi
