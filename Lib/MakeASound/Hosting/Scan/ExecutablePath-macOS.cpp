#include "ExecutablePath.h"

#include <mach-o/dyld.h>

#include <cstdint>
#include <cstring>

namespace MakeASound::Hosting
{

std::string executablePathFromPlatform()
{
    auto size = std::uint32_t {0};
    _NSGetExecutablePath(nullptr, &size);

    auto path = std::string(size, '\0');

    if (_NSGetExecutablePath(path.data(), &size) != 0)
        return {};

    path.resize(std::strlen(path.c_str()));
    return path;
}

} // namespace MakeASound::Hosting
