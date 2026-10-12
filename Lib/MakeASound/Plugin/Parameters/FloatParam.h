#pragma once

#include "Parameter.h"

#include <atomic>

namespace MakeASound
{

// A continuous value in [minValue, maxValue]; writes outside it are clamped.
class FloatParam : public Parameter
{
public:
    FloatParam(std::string_view nameToUse,
               float minToUse = 0.f,
               float maxToUse = 1.f,
               float defaultToUse = 0.5f,
               ParameterOptions options = {});

    float minValue() const noexcept override { return minimum; }
    float maxValue() const noexcept override { return maximum; }
    float defaultValue() const noexcept override { return initial; }

    void setValue(float plain) noexcept override;
    float getValue() const noexcept override { return get(); }

    float get() const noexcept { return value.load(std::memory_order_relaxed); }

private:
    float clamp(float plain) const noexcept;

    float minimum;
    float maximum;
    float initial;
    std::atomic<float> value;
};

} // namespace MakeASound
