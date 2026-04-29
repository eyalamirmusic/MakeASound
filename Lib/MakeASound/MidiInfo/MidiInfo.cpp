#include "MidiInfo.h"

#include <iomanip>
#include <sstream>

namespace MakeASound
{
namespace
{
std::string hexDump(const std::vector<std::uint8_t>& bytes)
{
    auto oss = std::ostringstream {};
    oss << '[' << std::hex << std::setfill('0');

    for (auto i = 0u; i < bytes.size(); ++i)
    {
        if (i > 0)
            oss << ' ';
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }

    oss << ']';
    return oss.str();
}
} // namespace

std::string formatMessage(const MidiMessage& message)
{
    auto& bytes = message.bytes;

    if (bytes.empty())
        return "(empty)";

    auto dump = hexDump(bytes);
    auto status = bytes[0] & 0xF0;
    auto channel = (bytes[0] & 0x0F) + 1;
    auto data1 = bytes.size() > 1 ? static_cast<int>(bytes[1]) : 0;
    auto data2 = bytes.size() > 2 ? static_cast<int>(bytes[2]) : 0;

    auto oss = std::ostringstream {};

    if (status == 0x80)
        oss << "Note Off    ch:" << channel << " note:" << data1
            << " vel:" << data2 << "  " << dump;
    else if (status == 0x90 && data2 == 0)
        oss << "Note Off    ch:" << channel << " note:" << data1
            << "        " << dump;
    else if (status == 0x90)
        oss << "Note On     ch:" << channel << " note:" << data1
            << " vel:" << data2 << "  " << dump;
    else if (status == 0xA0)
        oss << "Polytouch   ch:" << channel << " note:" << data1
            << " val:" << data2 << "  " << dump;
    else if (status == 0xB0)
        oss << "CC          ch:" << channel << " cc:" << data1
            << " val:" << data2 << "    " << dump;
    else if (status == 0xC0)
        oss << "Program     ch:" << channel << " prog:" << data1 << "             "
            << dump;
    else if (status == 0xD0)
        oss << "ChanPress   ch:" << channel << " val:" << data1 << "              "
            << dump;
    else if (status == 0xE0)
        oss << "Pitch Bend  ch:" << channel << " val:" << ((data2 << 7) | data1)
            << "     " << dump;
    else
        oss << "System      " << dump;

    return oss.str();
}

} // namespace MakeASound
