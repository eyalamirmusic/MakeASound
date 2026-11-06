#pragma once

/** @file
    MakeASound: audio devices, MIDI and real-time processing in C++20
    (namespace MakeASound). This umbrella header is MakeASoundCore.h plus the
    device layer: DeviceManager streams audio through miniaudio on every
    backend the machine offers, MidiManager opens MIDI ports (Core MIDI, WinMM,
    the ALSA sequencer), AudioSession follows the stream on iOS, and the UI
    headers give device and MIDI pickers.

    - The audio callback runs on an OS audio thread: no allocation, locks or
      logging there. Pass data in and out through SPSCQueue.
    - Errors are returned as values, never thrown.
    - DSP and plugin code that needs no device includes MakeASoundCore.h.
*/

#include "MakeASoundCore.h"
#include "Devices/DeviceManager.h"
#include "Devices/Engine.h"
#include "Devices/AudioSession.h"
#include "Devices/DeviceQueries.h"
#include "MIDI/MidiManager.h"
#include "MIDI/MidiBlockSync.h"
#include "MIDI/MIDI.h"
#include "UI/Dropdown.h"
#include "UI/UIDeviceManager.h"
#include "UI/UIMidiManager.h"
