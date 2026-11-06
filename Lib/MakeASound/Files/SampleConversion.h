#pragma once

/** @file
    The conversions every reader and writer here uses between float samples and
    integer ones. An integer sample of any depth is handled left-justified in an
    int32 (a 16-bit sample s is s * 65536), so one pair of rules serves them all.
*/

#include <cstdint>

namespace MakeASound::Files
{

/// Float to a full-scale int32: x <= -1 gives INT32_MIN, x >= 1 gives INT32_MAX, a
/// NaN gives 0, and anything else the nearest integer (ties to even) to
/// 2147483647 * x, the product computed in double. The writers then shift the
/// result right (arithmetically) to their depth.
std::int32_t floatToInt32(float sample) noexcept;
std::int32_t floatToInt32(double sample) noexcept;

/// A full-scale int32 to float: sample / 2^31, rounded once to the nearest float
/// (ties to even). Exact for every 8-, 16- and 24-bit sample.
float int32ToFloat(std::int32_t sample) noexcept;
} // namespace MakeASound::Files
