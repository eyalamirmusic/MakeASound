#pragma once

#include <cmath>

namespace MakeASound
{

// A plain range and the curve that maps it onto 0..1: JUCE's NormalisableRange
// without the custom functions, the arithmetic kept step for step in float so a
// value normalizes and denormalizes to the same bits, which is what a plugin
// ported from JUCE needs to keep its users' automation.
//
// `skew` below 1 gives the low end more of the travel, above 1 the high end;
// `symmetricSkew` applies it from the middle out to both ends. `interval` above
// 0 snaps every value onto `min + k * interval`.
struct FloatRange
{
    float min = 0.f;
    float max = 1.f;
    float interval = 0.f;
    float skew = 1.f;
    bool symmetricSkew = false;

    // The skew that puts `centre` at the middle of the travel (JUCE's
    // setSkewForCentre); `centre` must lie strictly inside the range.
    static FloatRange withCentre(float minToUse,
                                 float maxToUse,
                                 float centre,
                                 float interval = 0.f)
    {
        auto range = FloatRange {minToUse, maxToUse, interval};
        range.skew =
            std::log(0.5f) / std::log((centre - minToUse) / (maxToUse - minToUse));
        return range;
    }

    bool isLinear() const noexcept { return skew == 1.f; }

    // The step count a host sees: the grid's when the range is snapped and its
    // curve linear, so evenly spaced normalized steps land on it; 0 otherwise,
    // the values still snapping, as a JUCE float parameter's do.
    int numSteps() const noexcept
    {
        if (interval <= 0.f || !isLinear() || max <= min)
            return 0;

        return static_cast<int>((max - min) / interval);
    }

    float toNormalized(float plain) const noexcept
    {
        if (max <= min)
            return 0.f;

        auto proportion = clampUnit((plain - min) / (max - min));

        if (skew == 1.f)
            return proportion;

        if (!symmetricSkew)
            return std::pow(proportion, skew);

        auto distanceFromMiddle = 2.f * proportion - 1.f;

        return (1.f
                + std::pow(std::abs(distanceFromMiddle), skew)
                      * (distanceFromMiddle < 0.f ? -1.f : 1.f))
               / 2.f;
    }

    float toPlain(float normalized) const noexcept
    {
        auto proportion = clampUnit(normalized);

        if (!symmetricSkew)
        {
            if (skew != 1.f && proportion > 0.f)
                proportion = std::exp(std::log(proportion) / skew);

            return min + (max - min) * proportion;
        }

        auto distanceFromMiddle = 2.f * proportion - 1.f;

        if (skew != 1.f && distanceFromMiddle != 0.f)
            distanceFromMiddle =
                std::exp(std::log(std::abs(distanceFromMiddle)) / skew)
                * (distanceFromMiddle < 0.f ? -1.f : 1.f);

        return min + (max - min) / 2.f * (1.f + distanceFromMiddle);
    }

    // Onto the interval's grid, then into the range.
    float snap(float plain) const noexcept
    {
        if (interval > 0.f)
            plain = min + interval * std::floor((plain - min) / interval + 0.5f);

        return (plain <= min || max <= min) ? min : (plain >= max ? max : plain);
    }

private:
    // A NaN passes through, so the parameter's own check drops it.
    static float clampUnit(float value) noexcept
    {
        return value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
    }
};

} // namespace MakeASound
