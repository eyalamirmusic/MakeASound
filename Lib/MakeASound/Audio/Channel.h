#pragma once

#include <span>
#include <cstddef>

namespace MakeASound
{

// A non-owning view over a single channel's samples.
//
// This is the currency for passing one channel of audio around — used
// everywhere in place of a bare std::span<float>. It is a contiguous range, so
// range-for (`for (auto& sample : channel)`) and the standard algorithms
// (std::fill, std::copy, std::ranges::fill, ...) work directly on it.
class Channel
{
public:
    Channel() noexcept = default;
    explicit Channel(std::span<float> samplesToUse) noexcept;
    Channel(float* dataToUse, int numSamplesToUse) noexcept;

    int getNumSamples() const noexcept;
    bool isEmpty() const noexcept;

    float* data() const noexcept;
    float& operator[](std::size_t sample) const noexcept;

    // Sample iteration.
    float* begin() const noexcept;
    float* end() const noexcept;

private:
    std::span<float> samples;
};

} // namespace MakeASound
