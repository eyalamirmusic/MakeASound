#include "ALSA-Backend.h"

#include <algorithm>
#include <cerrno>
#include <chrono>

namespace MakeASound::ALSA
{
namespace
{
constexpr auto sysExStatus = std::uint8_t {0xF0};

// What counts as a MIDI port: anything else on a client is a control or a timer
// port that would only clutter a host's list.
constexpr auto midiPortTypes = SND_SEQ_PORT_TYPE_MIDI_GENERIC
                               | SND_SEQ_PORT_TYPE_SYNTH
                               | SND_SEQ_PORT_TYPE_APPLICATION;

unsigned int capabilitiesFor(Direction direction)
{
    // A port we read from is one the sequencer calls readable, and a port we
    // send to is one it calls writable: the names are the port's, not ours.
    if (direction == Direction::Input)
        return SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;

    return SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
}

bool isUsable(snd_seq_port_info_t* info, Direction direction)
{
    if ((snd_seq_port_info_get_type(info) & midiPortTypes) == 0)
        return false;

    auto needed = capabilitiesFor(direction);

    return (snd_seq_port_info_get_capability(info) & needed) == needed;
}
} // namespace

bool PortFilter::shouldSkip(const Address& address) const noexcept
{
    if (address.client != client)
        return false;

    return address.port == inputPort || address.port == outputPort;
}

Error getError(int result)
{
    if (result >= 0)
        return Error::NoError;

    switch (-result)
    {
        case ENOENT:
        case ENODEV:
        case ENXIO:
            return Error::INVALID_DEVICE;

        case EINVAL:
            return Error::INVALID_PARAMETER;

        case EPERM:
        case EACCES:
        case EBUSY:
            return Error::INVALID_USE;

        case ENOMEM:
        case EAGAIN:
        case ENOSPC:
            return Error::MEMORY_ERROR;

        case EIO:
        case EPIPE:
        case ECONNREFUSED:
        case ECONNRESET:
            return Error::DRIVER_ERROR;

        case ENOTTY:
        case ENOSYS:
            return Error::SYSTEM_ERROR;

        default:
            return Error::UNKNOWN_ERROR;
    }
}

MidiTimePoint toTimePoint(const snd_seq_event_t& event, MidiTimePoint queueEpoch)
{
    auto real =
        (event.flags & SND_SEQ_TIME_STAMP_MASK) == SND_SEQ_TIME_STAMP_REAL;

    const auto& time = event.time.time;

    // A port with no timestamping, or the first nanosecond of the queue: either
    // way the closest thing to a stamp is the moment it is being read.
    if (!real || (time.tv_sec == 0 && time.tv_nsec == 0))
        return std::chrono::steady_clock::now();

    auto nanos = std::chrono::seconds {time.tv_sec}
                 + std::chrono::nanoseconds {time.tv_nsec};

    return queueEpoch + std::chrono::duration_cast<MidiTimePoint::duration>(nanos);
}

std::string getPortName(snd_seq_client_info_t* client, snd_seq_port_info_t* port)
{
    const auto* clientName = snd_seq_client_info_get_name(client);
    const auto* portName = snd_seq_port_info_get_name(port);

    auto name = std::string {clientName != nullptr ? clientName : ""};

    name += ":";
    name += portName != nullptr ? portName : "";

    // The numeric pair earns its place: two ports of one device are told apart
    // by nothing else.
    name += " " + std::to_string(snd_seq_port_info_get_client(port));
    name += ":" + std::to_string(snd_seq_port_info_get_port(port));

    return name;
}

std::string getPortIdentity(const Address& address)
{
    return std::to_string(address.client) + ":" + std::to_string(address.port);
}

Vector<MidiPortInfo> getPorts(snd_seq_t* seq,
                              Direction direction,
                              const PortFilter& filter,
                              MidiPortRegistry& registry,
                              Vector<Address>& addresses)
{
    auto result = Vector<MidiPortInfo> {};

    addresses.clear();

    if (seq == nullptr)
        return result;

    registry.beginScan();

    snd_seq_client_info_t* clientInfo = nullptr;
    snd_seq_client_info_alloca(&clientInfo);

    snd_seq_port_info_t* portInfo = nullptr;
    snd_seq_port_info_alloca(&portInfo);

    snd_seq_client_info_set_client(clientInfo, -1);

    while (snd_seq_query_next_client(seq, clientInfo) >= 0)
    {
        auto client = snd_seq_client_info_get_client(clientInfo);

        // The system client owns the announce and timer ports, which carry no
        // MIDI.
        if (client == SND_SEQ_CLIENT_SYSTEM)
            continue;

        snd_seq_port_info_set_client(portInfo, client);
        snd_seq_port_info_set_port(portInfo, -1);

        while (snd_seq_query_next_port(seq, portInfo) >= 0)
        {
            auto address = Address {client, snd_seq_port_info_get_port(portInfo)};

            if (!isUsable(portInfo, direction) || filter.shouldSkip(address))
                continue;

            auto id = registry.idFor(getPortIdentity(address), result.size());

            result.add(MidiPortInfo {id, getPortName(clientInfo, portInfo)});
            addresses.add(address);
        }
    }

    return result;
}

Span<const std::uint8_t> getEventBytes(snd_midi_event_t* coder,
                                       const snd_seq_event_t& event,
                                       Span<std::uint8_t> buffer)
{
    if (event.type == SND_SEQ_EVENT_SYSEX)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(event.data.ext.ptr);
        auto length = static_cast<int>(event.data.ext.len);

        if (bytes == nullptr || length <= 0)
            return {};

        return {bytes, length};
    }

    if (coder == nullptr || buffer.empty())
        return {};

    auto written = snd_midi_event_decode(coder,
                                         buffer.data(),
                                         static_cast<long>(buffer.getSize()),
                                         &event);

    if (written <= 0)
        return {};

    return {buffer.data(), static_cast<int>(written)};
}

int buildEvent(snd_midi_event_t* coder,
               Span<const std::uint8_t> message,
               int offset,
               snd_seq_event_t& event)
{
    snd_seq_ev_clear(&event);

    auto remaining = message.size() - offset;

    if (message.empty() || offset < 0 || remaining <= 0)
        return 0;

    const auto* bytes = message.data() + offset;

    // Only the first fragment of a dump begins with 0xF0; the rest are data, so
    // what makes them a dump is where the message started.
    if (message[0] == sysExStatus)
    {
        auto chunk = std::min(remaining, maxSysExChunkBytes);

        // The event points straight at the caller's bytes, which the send copies
        // out before it returns: a dump of any size costs no buffer of ours.
        snd_seq_ev_set_sysex(&event, chunk, const_cast<std::uint8_t*>(bytes));

        return chunk;
    }

    if (coder == nullptr)
        return 0;

    auto taken = snd_midi_event_encode(coder, bytes, remaining, &event);

    if (taken <= 0 || event.type == SND_SEQ_EVENT_NONE)
    {
        // Half a message left in the coder would go out in front of the next
        // one, so an incomplete stream ends its life here.
        snd_midi_event_reset_encode(coder);
        return 0;
    }

    return static_cast<int>(taken);
}

bool isPortAdded(const snd_seq_event_t& event)
{
    return event.type == SND_SEQ_EVENT_PORT_START;
}

bool isPortRemoved(const snd_seq_event_t& event)
{
    return event.type == SND_SEQ_EVENT_PORT_EXIT;
}

} // namespace MakeASound::ALSA
