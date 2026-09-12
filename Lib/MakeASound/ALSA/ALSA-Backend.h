#pragma once

#include <alsa/asoundlib.h>
#include "../Common/Common.h"
#include "../MIDI/MidiInfo.h"
#include "../MIDI/MidiPortRegistry.h"
#include "../Devices/DeviceInfo.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace MakeASound::ALSA
{

enum class Direction
{
    Input,
    Output
};

// Where a sequencer port lives, and the identity its id is keyed on: ALSA
// addresses a port by a client:port pair and nothing else.
struct Address
{
    int client {-1};
    int port {-1};

    bool isValid() const noexcept { return client >= 0 && port >= 0; }
    bool operator==(const Address&) const = default;
};

// Our own plumbing, which a host has no business opening: the port every
// subscription lands on and the one every send leaves by. Virtual ports are not
// in here, so they enumerate like anyone else's.
struct PortFilter
{
    int client {-1};
    int inputPort {-1};
    int outputPort {-1};

    bool shouldSkip(const Address& address) const noexcept;
};

// alsa-lib answers with a negative errno throughout.
Error getError(int result);

// The event's own stamp on steady_clock, through the offset between the queue's
// realtime clock and that one taken when the queue started. An event the kernel
// stamped in ticks, or did not stamp at all, falls back to now.
MidiTimePoint toTimePoint(const snd_seq_event_t& event, MidiTimePoint queueEpoch);

// "<client name>:<port name> <client>:<port>", the string this library has always
// handed out here, so names do not move under anyone already reading them.
std::string getPortName(snd_seq_client_info_t* client, snd_seq_port_info_t* port);

std::string getPortIdentity(const Address& address);

// Every port that can carry MIDI in this direction: the system client skipped
// along with our own plumbing, then anything whose type or capabilities say no.
// `addresses` comes back parallel to the result, so resolving an id back to a
// client:port needs no second pass.
Vector<MidiPortInfo> getPorts(snd_seq_t* seq,
                              Direction direction,
                              const PortFilter& filter,
                              MidiPortRegistry& registry,
                              Vector<Address>& addresses);

// Room for the longest message the decoder can produce for anything that is not
// a SysEx fragment: song position and a quarter frame are the long ones.
constexpr auto maxShortMessageBytes = 12;

// A dump leaves as a run of fragments, which is how the sequencer carries one
// anyway - small enough that an event and its payload fit the library's output
// buffer and the destination's pool with room to spare.
constexpr auto maxSysExChunkBytes = 512;

// The wire bytes behind one event: a SysEx fragment is raw already, anything
// else is decoded into `buffer`. Empty for an event carrying no MIDI, which
// every announcement is.
Span<const std::uint8_t> getEventBytes(snd_midi_event_t* coder,
                                       const snd_seq_event_t& event,
                                       Span<std::uint8_t> buffer);

// As much of `message` from `offset` on as one event can carry, ready to be
// sent: a SysEx fragment pointing straight at the caller's memory, anything else
// encoded into the event itself. The whole message is passed rather than the
// rest of it because only its first byte says whether the fragments that follow
// belong to a dump. Returns the bytes it took, 0 for nothing complete to send.
int buildEvent(snd_midi_event_t* coder,
               Span<const std::uint8_t> message,
               int offset,
               snd_seq_event_t& event);

bool isPortAdded(const snd_seq_event_t& event);
bool isPortRemoved(const snd_seq_event_t& event);

} // namespace MakeASound::ALSA
