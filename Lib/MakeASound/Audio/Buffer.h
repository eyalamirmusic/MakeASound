#pragma once

#include "Channel.h"

namespace MakeASound
{

// A non-owning view over a planar (channel-major) audio block: all samples of
// channel 0, then all samples of channel 1, and so on.
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

    // Returned by value, not by reference to the Buffer, so it survives
    // iterating a temporary: `for (auto ch : info.getOutput().channels())`.
    PlanarView<float> channels() const noexcept { return view; }

    PlanarView<float>::Iterator begin() const noexcept { return view.begin(); }
    PlanarView<float>::Iterator end() const noexcept { return view.end(); }

private:
    PlanarView<float> view;
};

} // namespace MakeASound
