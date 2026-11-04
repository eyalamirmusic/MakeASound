#include "SampleConversion.h"
#include "ConversionRules.h"

namespace MakeASound::Files
{
std::int32_t floatToInt32(float sample) noexcept
{
    // Widening to double is exact, so the float and double rules agree.
    return Detail::toFullScale((double) sample);
}

std::int32_t floatToInt32(double sample) noexcept
{
    return Detail::toFullScale(sample);
}

float int32ToFloat(std::int32_t sample) noexcept
{
    return Detail::fromFullScale(sample);
}
} // namespace MakeASound::Files
