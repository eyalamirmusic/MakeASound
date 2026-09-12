#include "WinMIDI-Backend.h"

#include <chrono>
#include <cstring>

namespace MakeASound::WinMIDI
{
namespace
{
std::string toUtf8(const WCHAR* text)
{
    if (text == nullptr || text[0] == 0)
        return {};

    auto length =
        WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);

    if (length <= 0)
        return {};

    // `length` counts the terminator, which the string then trims back off.
    auto buffer = std::string(static_cast<std::size_t>(length), '\0');

    auto written = WideCharToMultiByte(CP_UTF8,
                                       0,
                                       text,
                                       -1,
                                       buffer.data(),
                                       length,
                                       nullptr,
                                       nullptr);

    if (written <= 0)
        return {};

    buffer.resize(std::strlen(buffer.c_str()));
    return buffer;
}

std::string getDeviceName(Direction direction, int portNumber)
{
    auto device = static_cast<UINT_PTR>(portNumber);

    if (direction == Direction::Input)
    {
        auto caps = MIDIINCAPSW {};

        if (midiInGetDevCapsW(device, &caps, static_cast<UINT>(sizeof(caps)))
            == MMSYSERR_NOERROR)
            return toUtf8(caps.szPname);

        return {};
    }

    auto caps = MIDIOUTCAPSW {};

    if (midiOutGetDevCapsW(device, &caps, static_cast<UINT>(sizeof(caps)))
        == MMSYSERR_NOERROR)
        return toUtf8(caps.szPname);

    return {};
}
} // namespace

Error getError(MMRESULT result)
{
    switch (result)
    {
        case MMSYSERR_NOERROR:
            return Error::NoError;

        case MMSYSERR_BADDEVICEID:
        case MMSYSERR_INVALHANDLE:
        case MMSYSERR_NODRIVER:
        case MIDIERR_NODEVICE:
            return Error::INVALID_DEVICE;

        // Someone else has the port. An ordinary desktop state rather than a
        // fault, and the nearest thing the shared enum has to "busy".
        case MMSYSERR_ALLOCATED:
        case MMSYSERR_HANDLEBUSY:
        case MIDIERR_STILLPLAYING:
        case MIDIERR_NOTREADY:
            return Error::INVALID_USE;

        case MMSYSERR_NOMEM:
            return Error::MEMORY_ERROR;

        case MMSYSERR_INVALPARAM:
        case MMSYSERR_INVALFLAG:
        case MIDIERR_UNPREPARED:
        case MIDIERR_INVALIDSETUP:
        case MIDIERR_BADOPENMODE:
            return Error::INVALID_PARAMETER;

        case MMSYSERR_NOTSUPPORTED:
        case MMSYSERR_NOTENABLED:
        case MMSYSERR_NODRIVERCB:
        case MIDIERR_NOMAP:
            return Error::DRIVER_ERROR;

        case MMSYSERR_ERROR:
            return Error::SYSTEM_ERROR;

        default:
            return Error::UNKNOWN_ERROR;
    }
}

MidiTimePoint toTimePoint(MidiTimePoint start, DWORD millisecondsSinceStart)
{
    auto elapsed = std::chrono::milliseconds {
        static_cast<std::int64_t>(millisecondsSinceStart)};

    return start + std::chrono::duration_cast<MidiTimePoint::duration>(elapsed);
}

int getShortMessageLength(std::uint8_t status)
{
    if (status < 0x80)
        return 0;

    if (status < 0xC0)
        return 3;

    if (status < 0xE0)
        return 2;

    if (status < 0xF0)
        return 3;

    switch (status)
    {
        case 0xF0:
            return 0;

        case 0xF1: // MTC quarter frame
        case 0xF3: // song select
            return 2;

        case 0xF2: // song position
            return 3;

        default:
            return 1;
    }
}

int unpackShortMessage(DWORD packed, std::array<std::uint8_t, 3>& out)
{
    auto status = static_cast<std::uint8_t>(packed & 0xFF);
    auto length = getShortMessageLength(status);
    auto* bytes = out.data();

    for (auto i = 0; i < length; ++i)
        bytes[i] = static_cast<std::uint8_t>((packed >> (8 * i)) & 0xFF);

    return length;
}

std::string getPortName(Direction direction, int portNumber)
{
    if (portNumber < 0 || portNumber >= getPortCount(direction))
        return {};

    return getDeviceName(direction, portNumber) + " " + std::to_string(portNumber);
}

int getPortCount(Direction direction)
{
    auto count = direction == Direction::Input ? midiInGetNumDevs()
                                               : midiOutGetNumDevs();

    return static_cast<int>(count);
}

Vector<MidiPortInfo> getPorts(Direction direction, MidiPortRegistry& registry)
{
    auto count = getPortCount(direction);
    auto result = Vector<MidiPortInfo> {};

    result.reserve(count);
    registry.beginScan();

    for (auto port = 0; port < count; ++port)
    {
        auto name = getPortName(direction, port);

        result.add(MidiPortInfo {registry.idFor(name, port), name});
    }

    return result;
}

} // namespace MakeASound::WinMIDI
