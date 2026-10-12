#pragma once

#include "Parameter.h"

#include <atomic>
#include <cmath>
#include <utility>

namespace MakeASound
{

// On or off. Saved as a JSON bool: the two labels are cosmetic, so a document
// never depends on them, though loading accepts them as text too.
class BoolParam : public Parameter
{
public:
    BoolParam(std::string_view nameToUse,
              bool defaultOn = false,
              ParameterOptions options = {})
        : BoolParam(nameToUse, defaultOn, "Off", "On", options)
    {
    }

    BoolParam(std::string_view nameToUse,
              bool defaultOn,
              std::string offTextToUse,
              std::string onTextToUse,
              ParameterOptions options = {})
        : Parameter(nameToUse, options)
        , offText(std::move(offTextToUse))
        , onText(std::move(onTextToUse))
        , initial(defaultOn)
        , state(defaultOn)
    {
    }

    float minValue() const noexcept override { return 0.f; }
    float maxValue() const noexcept override { return 1.f; }
    float defaultValue() const noexcept override { return initial ? 1.f : 0.f; }
    int numSteps() const noexcept override { return 1; }

    void setValue(float plain) noexcept override
    {
        if (std::isfinite(plain))
            setOn(plain >= 0.5f);
    }

    float getValue() const noexcept override { return isOn() ? 1.f : 0.f; }

    bool isOn() const noexcept { return state.load(std::memory_order_relaxed); }
    void setOn(bool on) noexcept { state.store(on, std::memory_order_relaxed); }

    std::string valueToText(float plain) const override
    {
        return plain >= 0.5f ? onText : offText;
    }

    float textToValue(std::string_view text) const override
    {
        if (auto on = parseState(text))
            return *on ? 1.f : 0.f;

        return defaultValue();
    }

    StateFormat stateFormat() const noexcept override { return StateFormat::Bool; }

    std::string toStateText() const override { return isOn() ? "true" : "false"; }

    bool fromStateText(std::string_view text) override
    {
        auto on = parseState(text);

        if (on)
            setOn(*on);

        return on.has_value();
    }

private:
    std::optional<bool> parseState(std::string_view text) const
    {
        auto word = trim(text);

        if (equalsIgnoringCase(word, onText) || equalsIgnoringCase(word, "true"))
            return true;

        if (equalsIgnoringCase(word, offText) || equalsIgnoringCase(word, "false"))
            return false;

        if (auto number = parseNumber(text); number && std::isfinite(*number))
            return *number >= 0.5f;

        return std::nullopt;
    }

    std::string offText;
    std::string onText;
    bool initial;
    std::atomic<bool> state;
};

} // namespace MakeASound
