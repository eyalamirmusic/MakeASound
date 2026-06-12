#pragma once

#include "Channel.h"

#include <span>
#include <cstddef>

namespace MakeASound
{

// A non-owning view over a planar (non-interleaved) multi-channel audio block.
//
// The underlying memory is laid out channel-major: all samples of channel 0,
// followed by all samples of channel 1, and so on. Buffer holds the flat span
// plus the channel count and slices it into per-channel Channels with the
// correct offsets, so callers never compute `channel * numSamples` by hand.
class Buffer
{
public:
    Buffer() noexcept = default;
    Buffer(std::span<float> dataToUse, int numChannelsToUse) noexcept;

    int getNumChannels() const noexcept;

    // Samples per channel.
    int getNumSamples() const noexcept;

    bool isEmpty() const noexcept;

    Channel getChannel(int channel) const noexcept;
    float* getChannelPointer(int channel) const noexcept;
    Channel operator[](int channel) const noexcept;

    // Yields one Channel per channel. Holds the (span, channel count) by value
    // rather than a pointer back to the Buffer, so it stays valid even when
    // iterating a temporary (e.g. `info.getOutput().channels()` — in C++20 that
    // Buffer temporary dies before the loop body runs).
    class ChannelIterator
    {
    public:
        ChannelIterator(std::span<float> dataToUse,
                        int numChannelsToUse,
                        int channelToUse) noexcept;

        Channel operator*() const noexcept;
        ChannelIterator& operator++() noexcept;
        bool operator!=(const ChannelIterator& other) const noexcept;

    private:
        std::span<float> data;
        int numChannels;
        int channel;
    };

    // A non-owning, non-allocating range over the channels, so callers can
    // write `for (auto channel : buffer.channels())`.
    class ChannelRange
    {
    public:
        ChannelRange(std::span<float> dataToUse, int numChannelsToUse) noexcept;

        ChannelIterator begin() const noexcept;
        ChannelIterator end() const noexcept;

    private:
        std::span<float> data;
        int numChannels;
    };

    ChannelRange channels() const noexcept;

private:
    std::span<float> data;
    int numChannels = 0;
};

} // namespace MakeASound
