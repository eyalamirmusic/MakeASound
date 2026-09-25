#pragma once

#include "Channel.h"

#include <algorithm>

namespace MakeASound
{

// A non-owning view over a planar (channel-major) audio block: all samples of
// channel 0, then all samples of channel 1, and so on. Channel c starts
// c * getChannelStride() samples after channel 0; the stride equals the
// channel length unless the buffer is a sub-range of a larger block.
class Buffer
{
public:
    Buffer() noexcept = default;

    // Splits one flat planar block evenly between the channels.
    Buffer(Span<float> dataToUse, int numChannelsToUse) noexcept
        : view(dataToUse, numChannelsToUse)
    {
    }

    Buffer(float* dataToUse, int numChannelsToUse, int numSamplesToUse) noexcept
        : view(dataToUse, numChannelsToUse, numSamplesToUse)
    {
    }

    Buffer(float* dataToUse,
           int numChannelsToUse,
           int numSamplesToUse,
           int channelStrideToUse) noexcept
        : view(dataToUse, numChannelsToUse, numSamplesToUse, channelStrideToUse)
    {
    }

    int getNumChannels() const noexcept { return view.getNumChannels(); }

    // Samples per channel.
    int getNumSamples() const noexcept { return view.getNumSamples(); }

    // Samples from the start of one channel to the start of the next.
    int getChannelStride() const noexcept { return view.getChannelStride(); }

    bool isContiguous() const noexcept { return view.isContiguous(); }

    bool isEmpty() const noexcept { return view.empty(); }

    Channel getChannel(int channel) const noexcept
    {
        return view.getChannel(channel);
    }

    float* getChannelPointer(int channel) const noexcept
    {
        return view.getChannelPointer(channel);
    }

    Channel operator[](int channel) const noexcept { return view[channel]; }

    // Samples [offset, offset + numSamples) of every channel, clamped to this
    // buffer's length.
    Buffer subBuffer(int offset, int numSamplesToUse) const noexcept
    {
        return Buffer(view.subView(offset, numSamplesToUse));
    }

    void clear() const noexcept { fill(0.0f); }

    void fill(float value) const noexcept { view.fill(value); }

    // Copies the channels and samples both buffers have; the rest of this
    // buffer is left as it was.
    void copyFrom(const Buffer& other) const noexcept
    {
        forEachSharedSample(other,
                            [](float& target, float source) { target = source; });
    }

    // Mixes the channels and samples both buffers have into this one.
    void addFrom(const Buffer& other) const noexcept
    {
        forEachSharedSample(other,
                            [](float& target, float source) { target += source; });
    }

    // Returned by value, not by reference to the Buffer, so it survives
    // iterating a temporary: `for (auto ch : info.getOutput().channels())`.
    PlanarView<float> channels() const noexcept { return view; }

    PlanarView<float>::Iterator begin() const noexcept { return view.begin(); }
    PlanarView<float>::Iterator end() const noexcept { return view.end(); }

private:
    explicit Buffer(PlanarView<float> viewToUse) noexcept
        : view(viewToUse)
    {
    }

    template <typename Operation>
    void forEachSharedSample(const Buffer& other, Operation operation) const noexcept
    {
        auto numChannels = std::min(getNumChannels(), other.getNumChannels());
        auto numSamples = std::min(getNumSamples(), other.getNumSamples());

        for (auto channel = 0; channel < numChannels; ++channel)
        {
            auto target = getChannel(channel);
            auto source = other.getChannel(channel);

            for (auto sample = 0; sample < numSamples; ++sample)
                operation(target[sample], source[sample]);
        }
    }

    PlanarView<float> view;
};

} // namespace MakeASound
