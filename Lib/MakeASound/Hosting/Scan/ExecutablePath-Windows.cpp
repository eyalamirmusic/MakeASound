#include "ExecutablePath.h"

#include <eacp/Core/Utils/FilePath.h>

#include <windows.h>

namespace MakeASound::Hosting
{

std::string executablePathFromPlatform()
{
    // Wide, so a path outside the ANSI code page survives. A buffer that is too
    // small is grown until the whole path fits.
    auto buffer = std::wstring(MAX_PATH, L'\0');

    for (;;)
    {
        const auto length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

        if (length == 0)
            return {};

        if (length < buffer.size())
        {
            buffer.resize(length);
            break;
        }

        buffer.resize(buffer.size() * 2);
    }

    return eacp::FilePath::fromWide(buffer).str();
}

} // namespace MakeASound::Hosting
