#include "ParameterChanges.h"
#include "../Common/Algorithms.h"

namespace MakeASound
{

float ParameterChange::valueAt(int sample) const noexcept
{
    auto k = sample - sampleOffset;

    if (rampSamples <= 0 || k >= rampSamples)
        return value;

    return previousValue
           + (value - previousValue) * static_cast<float>(k)
                 / static_cast<float>(rampSamples);
}

void ParameterChanges::sortByOffset() noexcept
{
    auto earlier = [](const ParameterChange& a, const ParameterChange& b) noexcept
    { return a.sampleOffset < b.sampleOffset; };

    Algorithms::stableInsertionSort(*this, earlier);
}

float ParameterChanges::valueAt(int index, int sample, float fallback) const noexcept
{
    auto current = static_cast<const ParameterChange*>(nullptr);

    for (const auto& change: *this)
    {
        if (change.index != index)
            continue;

        if (change.sampleOffset > sample)
            return current != nullptr ? current->valueAt(sample)
                                      : change.previousValue;

        current = &change;
    }

    return current != nullptr ? current->valueAt(sample) : fallback;
}

} // namespace MakeASound
