#pragma once

#include "Parameter.h"
#include "FloatRange.h"

#include <atomic>

namespace MakeASound
{

// A value in a FloatRange: writes outside it are clamped, and onto its interval
// when it has one. Normalizes through the range's curve, so a skewed range moves
// a host lane and an editor's slider the way JUCE's NormalisableRange would.
class FloatParam : public Parameter
{
public:
    FloatParam(std::string_view nameToUse,
               float minToUse = 0.f,
               float maxToUse = 1.f,
               float defaultToUse = 0.5f,
               ParameterOptions options = {});

    FloatParam(std::string_view nameToUse,
               FloatRange rangeToUse,
               float defaultToUse,
               ParameterOptions options = {});

    float minValue() const noexcept override { return valueRange.min; }
    float maxValue() const noexcept override { return valueRange.max; }
    float defaultValue() const noexcept override { return initial; }

    // The interval's grid when snapped and linear; 0 otherwise.
    int numSteps() const noexcept override { return valueRange.numSteps(); }

    void setValue(float plain) noexcept override;
    float getValue() const noexcept override { return get(); }

    float toNormalized(float plain) const noexcept override;
    float toPlain(float normalized) const noexcept override;
    Curve curve() const noexcept override;

    float get() const noexcept { return value.load(std::memory_order_relaxed); }

    const FloatRange& range() const noexcept { return valueRange; }

private:
    FloatRange valueRange;
    float initial;
    std::atomic<float> value;
};

} // namespace MakeASound
