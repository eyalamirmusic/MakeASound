#pragma once

#include "FloatParam.h"
#include "ChoiceParam.h"

#include <cmath>
#include <string>

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

    DecibelParam(std::string_view nameToUse,
                 FloatRange rangeDb,
                 float defaultDb,
                 ParameterOptions options = {})
        : FloatParam(nameToUse, rangeDb, defaultDb, options)
    {
    }

    float decibels() const noexcept { return get(); }
    float gain() const noexcept { return std::pow(10.f, get() / 20.f); }

    std::string valueToText(float db) const override
    {
        return formatNumber("%+.1f dB", db);
    }
};

// A frequency, skewed so the geometric mean √(min·max) sits at the middle of the
// travel (20 Hz–20 kHz puts 632 Hz there), or linear when the range starts at 0
// Hz or below. The FloatRange overload takes any other curve:
// `FloatRange {minHz, maxHz}` is linear, the mapping a Tamber Plug HzParam had,
// which a port keeps or every automation lane on it moves. Typed text may use
// kHz.
struct HzParam : FloatParam
{
    HzParam(std::string_view nameToUse,
            float minHz,
            float maxHz,
            float defaultHz,
            ParameterOptions options = {})
        : FloatParam(nameToUse, rangeFor(minHz, maxHz), defaultHz, options)
    {
    }

    HzParam(std::string_view nameToUse,
            FloatRange rangeHz,
            float defaultHz,
            ParameterOptions options = {})
        : FloatParam(nameToUse, rangeHz, defaultHz, options)
    {
    }

    static FloatRange rangeFor(float minHz, float maxHz) noexcept
    {
        if (minHz <= 0.f || maxHz <= minHz)
            return {minHz, maxHz};

        return FloatRange::withCentre(minHz, maxHz, std::sqrt(minHz * maxHz));
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

    TimeParam(std::string_view nameToUse,
              FloatRange rangeSeconds,
              float defaultSeconds,
              ParameterOptions options = {})
        : FloatParam(nameToUse, rangeSeconds, defaultSeconds, options)
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

// A -1..1 amount shown as a signed whole percentage: a modulation depth, where a
// negative one inverts the source.
struct BipolarParam : FloatParam
{
    BipolarParam(std::string_view nameToUse,
                 float defaultFraction,
                 ParameterOptions options = {})
        : FloatParam(nameToUse, -1.f, 1.f, defaultFraction, options)
    {
    }

    std::string valueToText(float fraction) const override
    {
        return formatNumber("%+.0f %%", fraction * 100.f);
    }

    float textToValue(std::string_view text) const override
    {
        auto percent = parseNumber(text);
        return percent ? *percent * 0.01f : defaultValue();
    }
};

// A pitch offset in semitones, -12..+12 unless given a range, shown signed and
// whole. The value stays continuous, so a host lane moves smoothly;
// semitones() rounds it.
struct SemitoneParam : FloatParam
{
    SemitoneParam(std::string_view nameToUse,
                  int defaultSemitones,
                  ParameterOptions options = {})
        : SemitoneParam(nameToUse, -12, 12, defaultSemitones, options)
    {
    }

    SemitoneParam(std::string_view nameToUse,
                  int minSemitones,
                  int maxSemitones,
                  int defaultSemitones,
                  ParameterOptions options = {})
        : FloatParam(nameToUse,
                     static_cast<float>(minSemitones),
                     static_cast<float>(maxSemitones),
                     static_cast<float>(defaultSemitones),
                     options)
    {
    }

    int semitones() const noexcept { return static_cast<int>(std::lround(get())); }

    std::string valueToText(float semitones) const override
    {
        return formatNumber("%+.0f st", static_cast<float>(std::lround(semitones)));
    }
};

// A MIDI note number, 0..127, shown whole. The value stays continuous, so a host
// lane moves smoothly; note() rounds it.
struct NoteParam : FloatParam
{
    NoteParam(std::string_view nameToUse,
              int defaultNote,
              ParameterOptions options = {})
        : FloatParam(nameToUse, 0.f, 127.f, static_cast<float>(defaultNote), options)
    {
    }

    int note() const noexcept { return static_cast<int>(std::lround(get())); }

    std::string valueToText(float note) const override
    {
        return formatNumber("%.0f", static_cast<float>(std::lround(note)));
    }
};

// A transpose in whole octaves, -2..+2 unless given a range: a choice per
// octave named by its signed offset ("-1", "0", "+1"), so saved state keeps the
// octave if the range grows. octaves() is the offset, semitones() the same in
// semitones.
struct OctaveParam : ChoiceParam
{
    explicit OctaveParam(std::string_view nameToUse,
                         int defaultOctaves = 0,
                         ParameterOptions options = {})
        : OctaveParam(nameToUse, -2, 2, defaultOctaves, options)
    {
    }

    OctaveParam(std::string_view nameToUse,
                int minOctaves,
                int maxOctaves,
                int defaultOctaves,
                ParameterOptions options = {})
        : ChoiceParam(nameToUse,
                      choicesFor(minOctaves, maxOctaves),
                      defaultOctaves - minOctaves,
                      options)
        , lowest(minOctaves)
    {
    }

    int octaves() const noexcept { return getIndex() + lowest; }
    int semitones() const noexcept { return octaves() * 12; }

private:
    static Vector<std::string> choicesFor(int minOctaves, int maxOctaves)
    {
        auto choices = Vector<std::string> {};

        for (auto octave = minOctaves; octave <= maxOctaves; ++octave)
            choices.add(octave > 0 ? '+' + std::to_string(octave)
                                   : std::to_string(octave));

        return choices;
    }

    int lowest;
};

} // namespace MakeASound
