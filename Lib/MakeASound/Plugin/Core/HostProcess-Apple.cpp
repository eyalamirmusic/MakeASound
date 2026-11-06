#include "HostProcess.h"

#include <cstdlib>

namespace MakeASound
{

std::string hostProcessName()
{
    const auto* name = getprogname();
    return name != nullptr ? std::string(name) : std::string();
}

} // namespace MakeASound
