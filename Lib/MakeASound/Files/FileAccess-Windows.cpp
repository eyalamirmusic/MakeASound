#include "FileAccess.h"

#include <share.h>
#include <stdio.h>

namespace MakeASound::Files::Detail
{
std::FILE* openFile(const std::filesystem::path& path, bool forWriting) noexcept
{
    // The wide-character path, so names outside the ANSI code page open too, and
    // shared like fopen's files are elsewhere (_wfopen_s would deny sharing).
    return _wfsopen(path.c_str(), forWriting ? L"wb" : L"rb", _SH_DENYNO);
}

bool seekFile(std::FILE* file, std::int64_t offset, int whence) noexcept
{
    return _fseeki64(file, (__int64) offset, whence) == 0;
}

std::int64_t tellFile(std::FILE* file) noexcept
{
    return (std::int64_t) _ftelli64(file);
}
} // namespace MakeASound::Files::Detail
