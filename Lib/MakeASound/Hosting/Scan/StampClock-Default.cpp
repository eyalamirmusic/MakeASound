#include "StampClock.h"

#include <chrono>

namespace MakeASound::Hosting
{

std::int64_t toStampSeconds(std::filesystem::file_time_type time) noexcept
{
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch())
        .count();
}

} // namespace MakeASound::Hosting
