#pragma once

// Both are set on the target too (see Lib/CMakeLists.txt), because a unity build
// concatenates this with every other TU and windows.h getting in first without
// them is what leaves min/max as macros. Kept here as well so the file still
// compiles on its own.
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <windows.h>
#include <mmsystem.h>

#include "../Common/Common.h"
#include "../MIDI/MidiInfo.h"
#include "../MIDI/MidiParser.h"
#include "../MIDI/MidiPortRegistry.h"
#include "../Devices/DeviceInfo.h"

#include <array>
#include <cstdint>
#include <string>

namespace MakeASound::WinMIDI
{

enum class Direction
{
    Input,
    Output
};

Error getError(MMRESULT result);

// WinMM counts a message's timestamp in whole milliseconds from midiInStart, so
// `start` is steady_clock as it was at that call and this is the only resolution
// an arrival can have on this platform — coarser than a hardware stamp, and all
// WinMM offers.
MidiTimePoint toTimePoint(MidiTimePoint start, DWORD millisecondsSinceStart);

// How many bytes of a short message the packed dword carries. 0 for a byte that
// is not a status byte, and for 0xF0, which only ever arrives as MIM_LONGDATA.
int getShortMessageLength(std::uint8_t status);

// MIM_DATA's dwParam1, low byte first, into `out`. Returns how many it held.
int unpackShortMessage(DWORD packed, std::array<std::uint8_t, 3>& out);

// szPname as UTF-8 with the port number appended, which is what RtMidi's WinMM
// backend handed out, so the strings do not move under anyone already reading
// them. It is also the port's whole identity: two interfaces of one model carry
// the same szPname and WinMM offers nothing else to tell them apart, so a
// replug that reorders the list does renumber the ports after it.
std::string getPortName(Direction direction, int portNumber);

int getPortCount(Direction direction);

Vector<MidiPortInfo> getPorts(Direction direction, MidiPortRegistry& registry);

} // namespace MakeASound::WinMIDI
