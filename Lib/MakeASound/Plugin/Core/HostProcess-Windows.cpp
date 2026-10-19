#include "HostProcess.h"

// A unity build concatenates this with TUs that use std::min and std::max.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <filesystem>
#include <vector>

namespace MakeASound
{

namespace
{
std::string wideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};

    auto size = WideCharToMultiByte(CP_UTF8,
                                    0,
                                    wide.data(),
                                    static_cast<int>(wide.size()),
                                    nullptr,
                                    0,
                                    nullptr,
                                    nullptr);

    if (size <= 0)
        return {};

    auto utf8 = std::string(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8,
                        0,
                        wide.data(),
                        static_cast<int>(wide.size()),
                        utf8.data(),
                        size,
                        nullptr,
                        nullptr);
    return utf8;
}
} // namespace

std::string hostProcessName()
{
    // Grown until the path fits: a long path is truncated without an error code
    // other than the buffer filling up.
    auto buffer = std::vector<wchar_t>(MAX_PATH);

    for (;;)
    {
        auto size = static_cast<int>(buffer.size());
        auto length = static_cast<int>(
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(size)));

        if (length == 0)
            return {};

        if (length < size)
        {
            auto path = std::filesystem::path(
                std::wstring(buffer.data(), static_cast<size_t>(length)));
            return wideToUtf8(path.stem().wstring());
        }

        if (size >= 32768)
            return {};

        buffer.resize(buffer.size() * 2);
    }
}

} // namespace MakeASound
