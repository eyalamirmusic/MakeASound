#include "HostProcess.h"

#include <filesystem>
#include <system_error>

namespace MakeASound
{

std::string hostProcessName()
{
    auto error = std::error_code {};
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);

    if (error)
        return {};

    return path.filename().string();
}

} // namespace MakeASound
