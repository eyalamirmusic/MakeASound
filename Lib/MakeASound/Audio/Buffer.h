#pragma once

#include "Channel.h"

namespace MakeASound
{

// A non-owning view over a planar (non-interleaved) multi-channel audio block.
//
// The underlying memory is laid out channel-major: all samples of channel 0,
// followed by all samples of channel 1, and so on. Buffer is a naming layer
// over EA::PlanarView<float>: it slices the block into per-channel Channels
// with the correct offsets, so callers never compute `channel * numSamples`
// by hand.
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

    int getNumChannels() const noexcept { return view.getNumChannels(); }

    // Samples per channel.
    int getNumSamples() const noexcept { return view.getNumSamples(); }

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

    // Yields one Channel per channel. The returned view carries the block's
    // shape by value rather than a pointer back to the Buffer, so it stays
    // valid even when iterating a temporary (e.g. `info.getOutput().channels()`
    // — in C++20 that Buffer temporary dies before the loop body runs).
    PlanarView<float> channels() const noexcept { return view; }

    // A Buffer is itself a range over its channels.
    PlanarView<float>::Iterator begin() const noexcept { return view.begin(); }
    PlanarView<float>::Iterator end() const noexcept { return view.end(); }

private:
    PlanarView<float> view;
};

} // namespace MakeASound
