#pragma once

// MakeASoundCore: the audio and MIDI vocabulary and the processing types, with no
// device backend behind them. MakeASound.h adds the devices on top.

#include "Common/Common.h"
#include "Realtime/SPSCQueue.h"
#include "Realtime/ScopedNoDenormals.h"
#include "Realtime/Smoother.h"
#include "Realtime/SpinLock.h"
#include "Audio/Processor.h"
#include "MIDI/MIDI.h"
#include "MIDI/MidiInfo.h"
#include "MIDI/MidiParser.h"
