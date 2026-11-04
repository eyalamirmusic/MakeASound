#include "FileAccess.h"

#include <sys/types.h>

namespace MakeASound::Files::Detail
{
// CMake defines _FILE_OFFSET_BITS=64 for this library, so off_t is 64 bits on
// 32-bit Linux too.
static_assert(sizeof(off_t) >= 8, "fseeko/ftello need a 64-bit off_t");

std::FILE* openFile(const std::filesystem::path& path, bool forWriting) noexcept
{
    return std::fopen(path.c_str(), forWriting ? "wb" : "rb");
}

bool seekFile(std::FILE* file, std::int64_t offset, int whence) noexcept
{
    return fseeko(file, (off_t) offset, whence) == 0;
}

std::int64_t tellFile(std::FILE* file) noexcept
{
    return (std::int64_t) ftello(file);
}
} // namespace MakeASound::Files::Detail
