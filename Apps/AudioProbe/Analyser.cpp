#include "Analyser.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace AudioProbe
{
namespace
{
constexpr auto bottomHz = 30.0;
constexpr auto topHz = 18000.0;

constexpr auto floorDb = -84.f;
constexpr auto ceilingDb = 0.f;

// A Hann window's coherent gain is 0.5, so a full-scale sine leaves
// amplitude * fftSize / 4 in its bin. Dividing by that puts it at 0dB.
constexpr auto magnitudeScale = 4.f / static_cast<float>(Analyser::fftSize);

float normalizedDb(float magnitude)
{
    auto db = 20.f * std::log10(std::max(magnitude, 1.0e-9f));
    return std::clamp((db - floorDb) / (ceilingDb - floorDb), 0.f, 1.f);
}
} // namespace

void Analyser::rebuildBands(int rate)
{
    auto nyquist = static_cast<double>(rate) * 0.5;
    auto top = std::min(topHz, nyquist * 0.92);
    auto ratio = std::log(top / bottomHz);

    for (auto i = 0; i < binCount; ++i)
    {
        auto low = static_cast<double>(i) / binCount;
        auto high = static_cast<double>(i + 1) / binCount;

        auto lowHz = bottomHz * std::exp(ratio * low);
        auto highHz = bottomHz * std::exp(ratio * high);

        auto perBin = static_cast<double>(rate) / fftSize;
        auto first = static_cast<int>(std::floor(lowHz / perBin));
        auto last = static_cast<int>(std::ceil(highHz / perBin));

        // Bin 0 is DC, which is not a frequency anybody wants to see.
        bands[i].first = std::clamp(first, 1, fftSize / 2 - 1);
        bands[i].last = std::clamp(std::max(last, first), 1, fftSize / 2 - 1);
    }

    bandRate = rate;
}

bool Analyser::drainIntoRing()
{
    auto sample = 0.f;
    auto drained = false;

    while (fifo.pop(sample))
    {
        ring[static_cast<std::size_t>(ringWrite)] = sample;
        ringWrite = (ringWrite + 1) % fftSize;
        drained = true;
    }

    return drained;
}

void Analyser::transform()
{
    if (!windowBuilt)
    {
        for (auto i = 0; i < fftSize; ++i)
        {
            auto phase = 2.0 * std::numbers::pi * i / (fftSize - 1);
            hann[static_cast<std::size_t>(i)] =
                static_cast<float>(0.5 - 0.5 * std::cos(phase));
        }

        windowBuilt = true;
    }

    for (auto i = 0; i < fftSize; ++i)
    {
        auto index = static_cast<std::size_t>((ringWrite + i) % fftSize);
        real[static_cast<std::size_t>(i)] =
            ring[index] * hann[static_cast<std::size_t>(i)];
        imag[static_cast<std::size_t>(i)] = 0.f;
    }

    for (auto i = 1, j = 0; i < fftSize; ++i)
    {
        auto bit = fftSize >> 1;

        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;

        j ^= bit;

        if (i < j)
        {
            std::swap(real[static_cast<std::size_t>(i)],
                      real[static_cast<std::size_t>(j)]);
            std::swap(imag[static_cast<std::size_t>(i)],
                      imag[static_cast<std::size_t>(j)]);
        }
    }

    for (auto span = 2; span <= fftSize; span <<= 1)
    {
        auto angle = -2.0 * std::numbers::pi / span;
        auto stepReal = static_cast<float>(std::cos(angle));
        auto stepImag = static_cast<float>(std::sin(angle));

        for (auto start = 0; start < fftSize; start += span)
        {
            auto twiddleReal = 1.f;
            auto twiddleImag = 0.f;

            for (auto offset = 0; offset < span / 2; ++offset)
            {
                auto even = static_cast<std::size_t>(start + offset);
                auto odd = static_cast<std::size_t>(start + offset + span / 2);

                auto oddReal = real[odd] * twiddleReal - imag[odd] * twiddleImag;
                auto oddImag = real[odd] * twiddleImag + imag[odd] * twiddleReal;

                real[odd] = real[even] - oddReal;
                imag[odd] = imag[even] - oddImag;
                real[even] += oddReal;
                imag[even] += oddImag;

                auto nextReal = twiddleReal * stepReal - twiddleImag * stepImag;
                twiddleImag = twiddleReal * stepImag + twiddleImag * stepReal;
                twiddleReal = nextReal;
            }
        }
    }

    for (auto band = 0; band < binCount; ++band)
    {
        auto peak = 0.f;

        for (auto i = bands[static_cast<std::size_t>(band)].first;
             i <= bands[static_cast<std::size_t>(band)].last;
             ++i)
        {
            auto index = static_cast<std::size_t>(i);
            auto magnitude = std::hypot(real[index], imag[index]) * magnitudeScale;
            peak = std::max(peak, magnitude);
        }

        measured[static_cast<std::size_t>(band)] = normalizedDb(peak);
    }
}

void Analyser::analyse(float deltaSeconds, int sampleRate)
{
    if (sampleRate <= 0)
        return;

    if (sampleRate != bandRate)
        rebuildBands(sampleRate);

    if (drainIntoRing())
        transform();

    auto falloff = deltaSeconds / 0.35f;
    auto loudest = 0.f;

    for (auto i = 0; i < binCount; ++i)
    {
        auto index = static_cast<std::size_t>(i);
        bins[index] = std::max(measured[index], bins[index] - falloff);
        loudest = std::max(loudest, bins[index]);
    }

    level = loudest;
}

} // namespace AudioProbe
