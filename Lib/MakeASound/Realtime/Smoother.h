#pragma once

#include "../Audio/Buffer.h"

#include <cmath>

namespace MakeASound
{

// One-pole exponential smoother that removes the zipper of block-rate parameter
// steps. The ramp time is when it arrives: the coefficient covers all but
// arrivalLevel of the distance by then, and the last step snaps onto the target.
class Smoother
{
public:
    void setSampleRate(int sampleRateToUse) noexcept
    {
        sampleRate = sampleRateToUse;
        updateRamp();
    }

    void setRampTime(float secondsToUse) noexcept
    {
        rampSeconds = secondsToUse;
        updateRamp();
    }

    // Lands on `value` with no glide, for prepare or a preset load.
    void reset(float value) noexcept
    {
        current = value;
        target = value;
        stepsLeft = 0;
    }

    void setTarget(float value) noexcept
    {
        if (value == target)
            return;

        target = value;
        stepsLeft = rampSamples;
    }

    float getTarget() const noexcept { return target; }
    float getCurrent() const noexcept { return current; }
    bool isSmoothing() const noexcept { return current != target; }

    float next() noexcept
    {
        if (current == target)
            return current;

        auto delta = target - current;
        auto settled = std::abs(delta) <= settleTolerance * (1.f + std::abs(target));

        if (--stepsLeft <= 0 || settled)
            current = target;
        else
            current += coefficient * delta;

        return current;
    }

    void fill(Channel output) noexcept
    {
        for (auto& sample: output)
            sample = next();
    }

    // Advances once per sample, so every channel shares the gain at each sample.
    void applyGain(Buffer& buffer) noexcept
    {
        if (!isSmoothing())
        {
            buffer.applyGain(current);
            return;
        }

        for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            auto gain = next();

            for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer[channel][sample] *= gain;
        }
    }

private:
    void updateRamp() noexcept
    {
        auto samples = static_cast<double>(rampSeconds) * sampleRate;
        rampSamples = samples > 0.0 ? static_cast<int>(std::lround(samples)) : 0;

        if (rampSamples == 0)
        {
            coefficient = 1.f;
            return;
        }

        auto perSample = std::log(arrivalLevel) / static_cast<float>(rampSamples);
        coefficient = 1.f - std::exp(perSample);
    }

    static constexpr float arrivalLevel = 1e-4f;
    static constexpr float settleTolerance = 1e-5f;

    int sampleRate = 44100;
    float rampSeconds = 0.f;

    int rampSamples = 0;
    int stepsLeft = 0;
    float coefficient = 1.f;
    float current = 0.f;
    float target = 0.f;
};

} // namespace MakeASound
