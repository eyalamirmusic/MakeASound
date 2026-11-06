#include "FloatParam.h"

#include <cassert>
#include <cmath>

namespace MakeASound
{

FloatParam::FloatParam(std::string_view nameToUse,
                       float minToUse,
                       float maxToUse,
                       float defaultToUse,
                       ParameterOptions options)
    : FloatParam(nameToUse, FloatRange {minToUse, maxToUse}, defaultToUse, options)
{
}

FloatParam::FloatParam(std::string_view nameToUse,
                       FloatRange rangeToUse,
                       float defaultToUse,
                       ParameterOptions options)
    : Parameter(nameToUse, options)
    , valueRange(rangeToUse)
    , initial(valueRange.snap(defaultToUse))
    , value(initial)
{
    assert(valueRange.min <= valueRange.max);
    assert(valueRange.interval >= 0.f);
    assert(valueRange.skew > 0.f && std::isfinite(valueRange.skew));
}

void FloatParam::setValue(float plain) noexcept
{
    // A NaN would pass any clamp and settle into filter state for the session.
    if (!std::isfinite(plain))
        return;

    value.store(valueRange.snap(plain), std::memory_order_relaxed);
}

// RangedAudioParameter's order: snap, then the curve; the curve, then snap.
float FloatParam::toNormalized(float plain) const noexcept
{
    return valueRange.toNormalized(valueRange.snap(plain));
}

float FloatParam::toPlain(float normalized) const noexcept
{
    auto clamped = normalized < 0.f ? 0.f : (normalized > 1.f ? 1.f : normalized);
    return valueRange.snap(valueRange.toPlain(clamped));
}

Parameter::Curve FloatParam::curve() const noexcept
{
    // Exact matches only: an AU host draws these curves with its own maths.
    const auto skew = valueRange.skew;

    if (skew == 1.f)
        return Curve::Linear;

    if (valueRange.symmetricSkew)
        return Curve::Other;

    if (skew == 0.5f)
        return Curve::SquareRoot;

    if (skew == 2.f)
        return Curve::Squared;

    if (skew == 3.f)
        return Curve::Cubed;

    if (skew == 1.f / 3.f)
        return Curve::CubeRoot;

    return Curve::Other;
}

} // namespace MakeASound
