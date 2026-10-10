#include "Buffer.h"

namespace MakeASound
{
namespace
{
// Each channel starts on a 64-byte boundary relative to the allocation, so a SIMD
// path can treat every channel alike.
constexpr auto channelAlignment = 16;

int alignedStride(int numSamples) noexcept
{
    return (numSamples + channelAlignment - 1) / channelAlignment * channelAlignment;
}
} // namespace

Buffer::Buffer(int numChannelsToUse, int numSamplesToUse)
{
    setSize(numChannelsToUse, numSamplesToUse);
}

Buffer Buffer::copyOf(const Buffer& source)
{
    auto copy = Buffer {source.getNumChannels(), source.getNumSamples()};
    copy.copyFrom(source);
    return copy;
}

void Buffer::setSize(int numChannelsToUse, int numSamplesToUse)
{
    numChannels = std::max(0, numChannelsToUse);
    numSamples = std::max(0, numSamplesToUse);

    auto stride = alignedStride(numSamples);

    samples.resize(numChannels * stride);
    table.resize(numChannels);

    for (auto channel = 0; channel < numChannels; ++channel)
        table[channel] = samples.data() + channel * stride;

    std::fill(samples.begin(), samples.end(), 0.0f);

    channels = table.data();
    startSample = 0;
    owning = true;
}

void Buffer::referTo(float* const* channelsToUse,
                     int numChannelsToUse,
                     int numSamplesToUse,
                     int startSampleToUse) noexcept
{
    releaseStorage();

    channels = channelsToUse;
    startSample = startSampleToUse;
    numChannels = numChannelsToUse;
    numSamples = numSamplesToUse;
}

void Buffer::moveFrom(Buffer& other) noexcept
{
    releaseStorage();

    samples = std::move(other.samples);
    table = std::move(other.table);
    owning = other.owning;

    // The table moved with its heap block, but the pointer into it is ours now.
    channels = owning ? table.data() : other.channels;
    startSample = other.startSample;
    numChannels = other.numChannels;
    numSamples = other.numSamples;

    other.channels = nullptr;
    other.startSample = 0;
    other.numChannels = 0;
    other.numSamples = 0;
    other.owning = false;
}

void Buffer::releaseStorage() noexcept
{
    if (!owning)
        return;

    samples = Vector<float> {};
    table = Vector<float*> {};
    owning = false;
}

} // namespace MakeASound
