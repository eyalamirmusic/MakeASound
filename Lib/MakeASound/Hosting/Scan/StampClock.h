#pragma once

#include <cstdint>
#include <filesystem>

namespace MakeASound::Hosting
{

// A file time as a stamp's `lastModified`, one TU per platform. Seconds since
// the Unix epoch on macOS (libc++'s file clock counts from it) and Linux
// (StampClock-Linux.cpp converts libstdc++'s, which counts from 2174); on
// Windows the file clock's own count from 1601, as stamps there always were
// (StampClock-Default.cpp).
std::int64_t toStampSeconds(std::filesystem::file_time_type time) noexcept;

} // namespace MakeASound::Hosting
