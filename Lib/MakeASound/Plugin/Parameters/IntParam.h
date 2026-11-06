#pragma once

#include "Parameter.h"

#include <atomic>
#include <cmath>
#include <string>

namespace MakeASound
{

// A whole number in [minimum, maximum], one step per value, shown and saved as
// an integer. Normalizes as JUCE's AudioParameterInt does.
class IntParam : public Parameter
{
public:
    IntParam(std::string_view nameToUse,
             int minimum,
             int maximum,
             int defaultToUse,
             ParameterOptions options = {})
        : Parameter(nameToUse, options)
        , low(minimum)
        , high(maximum < minimum ? minimum : maximum)
        , initial(clampInt(defaultToUse))
        , value(initial)
    {
    }

    float minValue() const noexcept override { return static_cast<float>(low); }
    float maxValue() const noexcept override { return static_cast<float>(high); }
    float defaultValue() const noexcept override
    {
        return static_cast<float>(initial);
    }

    int numSteps() const noexcept override { return high - low; }

    void setValue(float plain) noexcept override
    {
        if (!std::isfinite(plain))
            return;

        auto rounded = roundToIndex(plain < minValue()
                                        ? minValue()
                                        : (plain > maxValue() ? maxValue() : plain));
        value.store(static_cast<int>(rounded), std::memory_order_relaxed);
    }

    float getValue() const noexcept override { return static_cast<float>(get()); }

    int get() const noexcept { return value.load(std::memory_order_relaxed); }

    float toNormalized(float plain) const noexcept override
    {
        return indexToNormalized(plain);
    }

    float toPlain(float normalized) const noexcept override
    {
        return indexToPlain(normalized);
    }

    std::string valueToText(float plain) const override
    {
        auto shown = std::isfinite(plain) ? roundToIndex(plain) : defaultValue();
        auto text = std::to_string(static_cast<int>(shown));

        if (!label().empty())
            text += ' ' + label();

        return text;
    }

    float textToValue(std::string_view text) const override
    {
        auto typed = parseNumber(text);

        if (!typed || !std::isfinite(*typed))
            return defaultValue();

        return roundToIndex(*typed);
    }

    std::string toStateText() const override { return std::to_string(get()); }

private:
    int clampInt(int number) const noexcept
    {
        return number < low ? low : (number > high ? high : number);
    }

    int low;
    int high;
    int initial;
    std::atomic<int> value;
};

} // namespace MakeASound
