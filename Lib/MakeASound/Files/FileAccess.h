#pragma once

// Internal: the per-platform part of FileSource and FileSink. Opening a path and
// 64-bit seeks and tells differ between POSIX (fopen, fseeko, ftello) and Windows
// (_wfopen_s, _fseeki64, _ftelli64); FileAccess-Default.cpp and
// FileAccess-Windows.cpp each implement these, and CMake picks one.

#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace MakeASound::Files::Detail
{
// Opens 'path' in binary mode for reading, or for writing (created or truncated).
// Null on failure.
std::FILE* openFile(const std::filesystem::path& path, bool forWriting) noexcept;

// Seeks to an absolute offset (whence SEEK_SET) or relative to the end (SEEK_END).
bool seekFile(std::FILE* file, std::int64_t offset, int whence) noexcept;

// The current offset, or -1 on an error.
std::int64_t tellFile(std::FILE* file) noexcept;
} // namespace MakeASound::Files::Detail
