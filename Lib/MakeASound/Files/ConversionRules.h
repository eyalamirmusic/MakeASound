#pragma once

/** @file
    Internal: the float/int32 rules of SampleConversion.h as inline functions, so
    the per-sample loops of the readers and the writer can inline them.
*/

#include <cmath>
#include <cstdint>
#include <limits>

namespace MakeASound::Files::Detail
{
/// floatToInt32 inlined: NaN gives 0, the ends clamp, the rest rounds ties to even.
inline std::int32_t toFullScale(double sample) noexcept
{
    if (std::isnan(sample))
        return 0;

    if (sample <= -1.0)
        return std::numeric_limits<std::int32_t>::min();

    if (sample >= 1.0)
        return std::numeric_limits<std::int32_t>::max();

    // One rounding to double for the product, then to the nearest integer with
    // ties to even (the default rounding mode).
    auto product = 2147483647.0 * sample;
    return (std::int32_t) std::nearbyint(product);
}

/// int32ToFloat inlined: sample / 2^31, rounded once to float.
inline float fromFullScale(std::int32_t sample) noexcept
{
    // Exact in double; the only rounding is the conversion to float.
    return (float) ((double) sample / 2147483648.0);
}
} // namespace MakeASound::Files::Detail
