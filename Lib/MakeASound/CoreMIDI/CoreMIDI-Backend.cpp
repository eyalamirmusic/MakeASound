#include "CoreMIDI-Backend.h"

#include <mach/mach_time.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace MakeASound::CoreMIDI
{
namespace
{
// What one list carries once its own header and the packet's timestamp and
// length are paid for. Exactly what MIDIPacketListAdd accepts: 4082 bytes here.
constexpr auto listOverhead = static_cast<int>(offsetof(MIDIPacketList, packet)
                                               + offsetof(MIDIPacket, data));

constexpr auto maxListPayload = PacketListBuffer::bytes - listOverhead;

const mach_timebase_info_data_t& getTimebase()
{
    static const auto info = []
    {
        auto value = mach_timebase_info_data_t {};
        mach_timebase_info(&value);
        return value;
    }();

    return info;
}

// Divide before multiplying: the raw counter is wide enough that ticks * numer
// is a live overflow question on arm64.
std::int64_t machToNanos(std::int64_t ticks)
{
    const auto& info = getTimebase();

    auto numer = static_cast<std::int64_t>(info.numer);
    auto denom = static_cast<std::int64_t>(info.denom);

    return (ticks / denom) * numer + (ticks % denom) * numer / denom;
}

std::string toStdString(CFStringRef value)
{
    if (value == nullptr)
        return {};

    auto length = CFStringGetLength(value);
    auto capacity =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;

    auto buffer = std::string(static_cast<std::size_t>(capacity), '\0');

    if (!CFStringGetCString(value, buffer.data(), capacity, kCFStringEncodingUTF8))
        return {};

    buffer.resize(std::strlen(buffer.c_str()));
    return buffer;
}

// MIDIObjectGetStringProperty hands back a +1 reference, so every read owns one.
std::string getStringProperty(MIDIObjectRef object, CFStringRef property)
{
    if (object == 0)
        return {};

    auto value = CFStringRef {};

    if (MIDIObjectGetStringProperty(object, property, &value) != noErr)
        return {};

    auto result = toStdString(value);

    if (value != nullptr)
        CFRelease(value);

    return result;
}

std::string trim(std::string text)
{
    auto first = text.find_first_not_of(' ');

    if (first == std::string::npos)
        return {};

    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

// What RtMidi's Core MIDI backend builds when it names a port: the endpoint's
// own name with the device's prepended, unless the driver already did that.
std::string composeName(MIDIEndpointRef endpoint)
{
    auto name = trim(getStringProperty(endpoint, kMIDIPropertyName));

    auto entity = MIDIEntityRef {};

    // No entity means a virtual endpoint: its own name is all there is.
    if (MIDIEndpointGetEntity(endpoint, &entity) != noErr || entity == 0)
        return name;

    if (name.empty())
        name = trim(getStringProperty(entity, kMIDIPropertyName));

    auto device = MIDIDeviceRef {};

    if (MIDIEntityGetDevice(entity, &device) != noErr || device == 0)
        return name;

    auto deviceName = trim(getStringProperty(device, kMIDIPropertyName));

    if (deviceName.empty())
        return name;

    if (name.empty())
        return deviceName;

    // Some drivers put the device name in the endpoint's already.
    if (name.rfind(deviceName, 0) == 0)
        return name;

    return deviceName + " " + name;
}
} // namespace

Error getError(OSStatus status)
{
    switch (status)
    {
        case noErr:
            return Error::NoError;

        case kMIDIInvalidClient:
        case kMIDIInvalidPort:
        case kMIDIWrongEndpointType:
        case kMIDINoConnection:
        case kMIDIUnknownEndpoint:
        case kMIDIObjectNotFound:
            return Error::INVALID_DEVICE;

        case kMIDIUnknownProperty:
        case kMIDIWrongPropertyType:
        case kMIDIIDNotUnique:
            return Error::INVALID_PARAMETER;

        // The simulator and a sandbox without the entitlement both refuse
        // virtual endpoints this way; the client itself is fine.
        case kMIDINotPermitted:
            return Error::INVALID_USE;

        case kMIDIMessageSendErr:
            return Error::DRIVER_ERROR;

        case kMIDIWrongThread:
            return Error::THREAD_ERROR;

        case kMIDINoCurrentSetup:
        case kMIDIServerStartErr:
        case kMIDISetupFormatErr:
            return Error::SYSTEM_ERROR;

        default:
            return Error::UNKNOWN_ERROR;
    }
}

ClockAnchor::ClockAnchor()
    : steady(std::chrono::steady_clock::now())
    , mach(mach_absolute_time())
{
}

MidiTimePoint ClockAnchor::toTimePoint(MIDITimeStamp stamp) const
{
    if (stamp == 0)
        return steady;

    // A signed distance from the anchor, so a stamp scheduled ahead of it works
    // as well as one already past.
    auto ticks = static_cast<std::int64_t>(stamp) - static_cast<std::int64_t>(mach);
    auto nanos = std::chrono::nanoseconds {machToNanos(ticks)};

    return steady + std::chrono::duration_cast<MidiTimePoint::duration>(nanos);
}

MidiTimePoint toTimePoint(MIDITimeStamp stamp)
{
    return ClockAnchor {}.toTimePoint(stamp);
}

std::string getPortName(MIDIEndpointRef endpoint)
{
    auto display = trim(getStringProperty(endpoint, kMIDIPropertyDisplayName));

    return display.empty() ? composeName(endpoint) : display;
}

std::string getPortIdentity(MIDIEndpointRef endpoint)
{
    auto unique = SInt32 {};

    if (MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &unique)
        == noErr)
        return std::to_string(unique);

    // Nothing stable to key on, so fall back to what RtMidi keyed on all along.
    return getPortName(endpoint);
}

int getPortCount(Direction direction)
{
    auto count = direction == Direction::Input ? MIDIGetNumberOfSources()
                                               : MIDIGetNumberOfDestinations();

    return static_cast<int>(count);
}

MIDIEndpointRef getEndpoint(Direction direction, int portNumber)
{
    if (portNumber < 0 || portNumber >= getPortCount(direction))
        return 0;

    auto index = static_cast<ItemCount>(portNumber);

    return direction == Direction::Input ? MIDIGetSource(index)
                                         : MIDIGetDestination(index);
}

Vector<MidiPortInfo> getPorts(Direction direction, MidiPortRegistry& registry)
{
    auto count = getPortCount(direction);
    auto result = Vector<MidiPortInfo> {};

    result.reserve(count);
    registry.beginScan();

    for (auto port = 0; port < count; ++port)
    {
        auto endpoint = getEndpoint(direction, port);
        auto id = registry.idFor(getPortIdentity(endpoint), port);

        result.add(MidiPortInfo {id, getPortName(endpoint)});
    }

    return result;
}

void forEachPacket(const MIDIPacketList* list, const PacketSink& sink)
{
    if (list == nullptr)
        return;

    // One anchor for the whole list: the two clocks drift apart across a sleep,
    // so an offset is only good for about as long as this callback.
    auto anchor = ClockAnchor {};
    const auto* packet = &list->packet[0];

    for (auto i = UInt32 {}; i < list->numPackets; ++i)
    {
        if (packet->length > 0)
            sink(Span<const std::uint8_t>(packet->data, packet->length),
                 anchor.toTimePoint(packet->timeStamp));

        packet = MIDIPacketNext(packet);
    }
}

int buildPacketList(PacketListBuffer& buffer,
                    const std::uint8_t* bytes,
                    int size,
                    MIDITimeStamp timestamp)
{
    auto* list = buffer.get();
    auto* packet = MIDIPacketListInit(list);

    // MIDIPacketListAdd coalesces adds that share a timestamp into one packet,
    // so splitting the payload up before handing it over builds exactly what a
    // single add does. The list's own size is the only real limit.
    auto chunk = std::min(size, maxListPayload);

    if (MIDIPacketListAdd(list,
                          static_cast<ByteCount>(PacketListBuffer::bytes),
                          packet,
                          timestamp,
                          static_cast<ByteCount>(chunk),
                          bytes)
        == nullptr)
        return 0;

    return chunk;
}

} // namespace MakeASound::CoreMIDI
