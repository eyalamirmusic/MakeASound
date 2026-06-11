#include "Buffer.h"

namespace MakeASound
{

namespace
{
// Slices the planar block into a single channel. Shared by Buffer::getChannel
// and ChannelIterator so the offset maths lives in exactly one place.
Channel sliceChannel(std::span<float> data, int numChannels, int channel) noexcept
{
    if (numChannels <= 0)
        return {};

    auto numSamples = data.size() / static_cast<size_t>(numChannels);
    auto offset = static_cast<size_t>(channel) * numSamples;

    return Channel {data.subspan(offset, numSamples)};
}
} // namespace

Buffer::Buffer(std::span<float> dataToUse, int numChannelsToUse) noexcept
    : data(dataToUse)
    , numChannels(numChannelsToUse)
{
}

int Buffer::getNumChannels() const noexcept
{
    return numChannels;
}

int Buffer::getNumSamples() const noexcept
{
    if (numChannels <= 0)
        return 0;

    return static_cast<int>(data.size()) / numChannels;
}

bool Buffer::isEmpty() const noexcept
{
    return numChannels <= 0 || data.empty();
}

Channel Buffer::getChannel(int channel) const noexcept
{
    return sliceChannel(data, numChannels, channel);
}

float* Buffer::getChannelPointer(int channel) const noexcept
{
    return getChannel(channel).data();
}

Channel Buffer::operator[](int channel) const noexcept
{
    return getChannel(channel);
}

Buffer::ChannelRange Buffer::channels() const noexcept
{
    return {data, numChannels};
}

// --- ChannelRange ---

Buffer::ChannelRange::ChannelRange(std::span<float> dataToUse,
                                   int numChannelsToUse) noexcept
    : data(dataToUse)
    , numChannels(numChannelsToUse)
{
}

Buffer::ChannelIterator Buffer::ChannelRange::begin() const noexcept
{
    return {data, numChannels, 0};
}

Buffer::ChannelIterator Buffer::ChannelRange::end() const noexcept
{
    return {data, numChannels, numChannels};
}

// --- ChannelIterator ---

Buffer::ChannelIterator::ChannelIterator(std::span<float> dataToUse,
                                         int numChannelsToUse,
                                         int channelToUse) noexcept
    : data(dataToUse)
    , numChannels(numChannelsToUse)
    , channel(channelToUse)
{
}

Channel Buffer::ChannelIterator::operator*() const noexcept
{
    return sliceChannel(data, numChannels, channel);
}

Buffer::ChannelIterator& Buffer::ChannelIterator::operator++() noexcept
{
    ++channel;
    return *this;
}

bool Buffer::ChannelIterator::operator!=(const ChannelIterator& other) const noexcept
{
    return channel != other.channel;
}

} // namespace MakeASound
