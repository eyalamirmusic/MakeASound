#pragma once

#include "FloatParam.h"

#include <cmath>

namespace MakeASound
{

// A level in decibels. gain() is the linear factor the DSP multiplies by.
struct DecibelParam : FloatParam
{
    DecibelParam(std::string_view nameToUse,
                 float minDb,
                 float maxDb,
                 float defaultDb,
                 ParameterOptions options = {})
        : FloatParam(nameToUse, minDb, maxDb, defaultDb, options)
    {
    }

    float decibels() const noexcept { return get(); }
    float gain() const noexcept { return std::pow(10.f, get() / 20.f); }

    std::string valueToText(float db) const override
    {
        return formatNumber("%+.1f dB", db);
    }
};

// A linearly mapped frequency. Typed text may use kHz.
struct HzParam : FloatParam
{
    HzParam(std::string_view nameToUse,
            float minHz,
            float maxHz,
            float defaultHz,
            ParameterOptions options = {})
        : FloatParam(nameToUse, minHz, maxHz, defaultHz, options)
    {
    }

    std::string valueToText(float hz) const override
    {
        return formatNumber("%.2f Hz", hz);
    }

    float textToValue(std::string_view text) const override
    {
        auto typed = parseNumber(text);

        if (!typed)
            return defaultValue();

        return containsIgnoringCase(text, "khz") ? *typed * 1000.f : *typed;
    }
};

// A time in seconds, shown in ms below a second.
struct TimeParam : FloatParam
{
    TimeParam(std::string_view nameToUse,
              float minSeconds,
              float maxSeconds,
              float defaultSeconds,
              ParameterOptions options = {})
        : FloatParam(nameToUse, minSeconds, maxSeconds, defaultSeconds, options)
    {
    }

    std::string valueToText(float seconds) const override
    {
        if (seconds < 1.f)
            return formatNumber("%.0f ms", seconds * 1000.f);

        return formatNumber("%.2f s", seconds);
    }

    float textToValue(std::string_view text) const override
    {
        auto typed = parseNumber(text);

        if (!typed)
            return defaultValue();

        if (containsIgnoringCase(text, "ms"))
            return *typed * 0.001f;

        // A bare number reads in whichever unit lands inside the range, so
        // trimming "500 ms" to "300" means 300 ms rather than five minutes.
        if (containsIgnoringCase(text, "s") || isWithinRange(*typed))
            return *typed;

        return *typed * 0.001f;
    }

private:
    bool isWithinRange(float seconds) const noexcept
    {
        return seconds >= minValue() && seconds <= maxValue();
    }
};

// A 0..1 amount shown as a whole percentage.
struct PercentParam : FloatParam
{
    PercentParam(std::string_view nameToUse,
                 float defaultFraction,
                 ParameterOptions options = {})
        : FloatParam(nameToUse, 0.f, 1.f, defaultFraction, options)
    {
    }

    std::string valueToText(float fraction) const override
    {
        return formatNumber("%.0f %%", fraction * 100.f);
    }

    float textToValue(std::string_view text) const override
    {
        auto percent = parseNumber(text);
        return percent ? *percent * 0.01f : defaultValue();
    }
};

} // namespace MakeASound
