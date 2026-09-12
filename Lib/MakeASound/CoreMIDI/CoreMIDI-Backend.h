#pragma once

#include <CoreMIDI/CoreMIDI.h>
#include "../Common/Common.h"
#include "../MIDI/MidiInfo.h"
#include "../MIDI/MidiParser.h"
#include "../MIDI/MidiPortRegistry.h"
#include "../Devices/DeviceInfo.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace MakeASound::CoreMIDI
{

enum class Direction
{
    Input,
    Output
};

Error getError(OSStatus status);

// mach_absolute_time and steady_clock are not the same counter: steady_clock
// reads CLOCK_MONOTONIC_RAW, which keeps running while the machine sleeps, and
// mach_absolute_time does not. So the offset between them is measured rather
// than assumed to be zero, and measured fresh — one read of each, per packet
// list — so no sleep can put a packet hours in the past.
struct ClockAnchor
{
    ClockAnchor();

    // A stamp of 0 is Core MIDI's "as soon as you can", which has already
    // happened by the time a receive block is looking at it.
    MidiTimePoint toTimePoint(MIDITimeStamp stamp) const;

    MidiTimePoint steady {};
    std::uint64_t mach {};
};

// One conversion, anchored on the spot.
MidiTimePoint toTimePoint(MIDITimeStamp stamp);

// kMIDIPropertyDisplayName, falling back to the "device name + endpoint name"
// composition RtMidi's Core MIDI backend hands out, so names do not move under
// anyone who was already reading them.
std::string getPortName(MIDIEndpointRef endpoint);

// kMIDIPropertyUniqueID as a string: the only identity that survives a replug.
std::string getPortIdentity(MIDIEndpointRef endpoint);

int getPortCount(Direction direction);
MIDIEndpointRef getEndpoint(Direction direction, int portNumber);

Vector<MidiPortInfo> getPorts(Direction direction, MidiPortRegistry& registry);

// Each packet's raw MIDI 1.0 bytes with the packet's own timestamp. Pure, so it
// runs on Core MIDI's receive thread like everything else on that path.
using PacketSink = MidiSink<Span<const std::uint8_t>, MidiTimePoint>;
void forEachPacket(const MIDIPacketList* list, const PacketSink& sink);

// Room for one list, aligned the way MIDIPacketList has to be on ARM, and small
// enough to sit on the stack of a send made from an audio callback.
struct PacketListBuffer
{
    static constexpr int bytes = 4096;

    MIDIPacketList* get() noexcept
    {
        return reinterpret_cast<MIDIPacketList*>(storage);
    }

    // Left uninitialised on purpose: MIDIPacketListInit writes everything that
    // is read, and zeroing 4 KiB per send is not free on the audio thread.
    alignas(8) std::uint8_t storage[bytes];
};

// Fills `buffer` with a single packet carrying as much of `bytes` as the list
// holds, so a dump longer than that goes out as a run of lists. Returns how many
// of `bytes` this one took; 0 means none of it fit.
int buildPacketList(PacketListBuffer& buffer,
                    const std::uint8_t* bytes,
                    int size,
                    MIDITimeStamp timestamp);

} // namespace MakeASound::CoreMIDI
