#include "ExecutablePath.h"
#include "../PathText.h"

#include <filesystem>

namespace MakeASound::Hosting
{

std::string executablePathFromPlatform()
{
    auto ec = std::error_code();
    const auto path = std::filesystem::read_symlink("/proc/self/exe", ec);

    return ec ? std::string() : pathText(path);
}

} // namespace MakeASound::Hosting
