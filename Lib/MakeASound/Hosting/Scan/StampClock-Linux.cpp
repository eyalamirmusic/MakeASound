#include "StampClock.h"

#include <chrono>

namespace MakeASound::Hosting
{

std::int64_t toStampSeconds(std::filesystem::file_time_type time) noexcept
{
    const auto sinceUnix = std::chrono::file_clock::to_sys(time).time_since_epoch();

    return std::chrono::duration_cast<std::chrono::seconds>(sinceUnix).count();
}

} // namespace MakeASound::Hosting
