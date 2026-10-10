#pragma once

#include "Channel.h"

#include <algorithm>

namespace MakeASound
{

// A planar block of float samples that either owns its storage or refers to
// storage owned by someone else: a host's channel array, another Buffer, the
// backend's scratch. Every access goes through a table of per-channel pointers
// plus a start offset, so a sub-range or a channel subset is another Buffer
// over the same table with no storage of its own and no channel cap.
//
// Move-only. A copy would have to decide between aliasing and allocating, and
// neither is right on an audio thread, so copying is spelled out: copyOf() for
// a deep copy, getSubBuffer(0) for an alias. Everything that does not allocate
// is noexcept; the four members that can allocate are the two owning
// constructors, setSize and copyOf.
//
// Constness lives in the reference: a const Buffer hands out ConstChannels and
// cannot be sliced, since the slice would be writable.
class Buffer
{
public:
    template <typename T>
    class ChannelIterator;

    using Iterator = ChannelIterator<float>;
    using ConstIterator = ChannelIterator<const float>;

    Buffer() noexcept = default;

    // Owning, zeroed.
    Buffer(int numChannelsToUse, int numSamplesToUse);

    // Referring: channel c is channelsToUse[c] + startSampleToUse.
    Buffer(float* const* channelsToUse,
           int numChannelsToUse,
           int numSamplesToUse,
           int startSampleToUse = 0) noexcept
        : channels(channelsToUse)
        , startSample(startSampleToUse)
        , numChannels(numChannelsToUse)
        , numSamples(numSamplesToUse)
    {
    }

    Buffer(Buffer&& other) noexcept { moveFrom(other); }

    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other)
            moveFrom(other);

        return *this;
    }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    static Buffer copyOf(const Buffer& source);

    // Gives the buffer storage of its own, zeroed, dropping anything it referred
    // to. Capacity is never released, so after one call at the largest shape every
    // equal-or-smaller call is allocation-free.
    void setSize(int numChannelsToUse, int numSamplesToUse);

    // Drops owned storage and refers instead.
    void referTo(float* const* channelsToUse,
                 int numChannelsToUse,
                 int numSamplesToUse,
                 int startSampleToUse = 0) noexcept;

    bool isOwning() const noexcept { return owning; }
    bool isEmpty() const noexcept { return numChannels <= 0 || numSamples <= 0; }
    int getNumChannels() const noexcept { return numChannels; }
    int getNumSamples() const noexcept { return numSamples; }

    Channel getChannel(int channel) noexcept
    { return {channels[channel] + startSample, numSamples}; }

    ConstChannel getChannel(int channel) const noexcept
    { return {channels[channel] + startSample, numSamples}; }

    Channel operator[](int channel) noexcept { return getChannel(channel); }

    ConstChannel operator[](int channel) const noexcept
    { return getChannel(channel); }

    float* getChannelPointer(int channel) noexcept
    { return channels[channel] + startSample; }

    const float* getChannelPointer(int channel) const noexcept
    { return channels[channel] + startSample; }

    // The table the channels are read through, with the offset reported beside
    // it rather than applied: a buffer over sample 0 of its table (every owning
    // buffer, and anything a host handed over) can pass this straight to a C API.
    float* const* getChannelPointers() const noexcept { return channels; }
    int getStartSample() const noexcept { return startSample; }

    // Referring buffers over part of this one, clamped to its shape.
    Buffer getSubBuffer(int start, int numSamplesToUse) noexcept
    {
        auto first = std::clamp(start, 0, numSamples);
        auto length = std::clamp(numSamplesToUse, 0, numSamples - first);
        return {channels, numChannels, length, startSample + first};
    }

    Buffer getSubBuffer(int start) noexcept
    { return getSubBuffer(start, numSamples - start); }

    Buffer getChannelSubset(int firstChannel, int numChannelsToUse) noexcept
    {
        auto first = std::clamp(firstChannel, 0, numChannels);
        auto count = std::clamp(numChannelsToUse, 0, numChannels - first);
        return {channels + first, count, numSamples, startSample};
    }

    Buffer getSingleChannel(int channel) noexcept
    { return getChannelSubset(channel, 1); }

    void clear() noexcept { fill(0.0f); }

    void fill(float value) noexcept
    {
        for (auto channel: *this)
            channel.fill(value);
    }

    // Copies the channels and samples both buffers have; the rest is left as it was.
    void copyFrom(const Buffer& other) noexcept
    {
        forEachSharedChannel(
            other,
            [](Channel target, ConstChannel source, int count)
            { std::copy_n(source.begin(), count, target.begin()); });
    }

    void addFrom(const Buffer& other, float gain = 1.0f) noexcept
    {
        forEachSharedChannel(other,
                             [gain](Channel target, ConstChannel source, int count)
                             {
                                 for (auto i = 0; i < count; ++i)
                                     target[i] += source[i] * gain;
                             });
    }

    void applyGain(float gain) noexcept
    {
        for (auto channel: *this)
            for (auto& sample: channel)
                sample *= gain;
    }

    Iterator begin() noexcept { return {channels, startSample, numSamples, 0}; }

    Iterator end() noexcept
    { return {channels, startSample, numSamples, numChannels}; }

    ConstIterator begin() const noexcept
    { return {channels, startSample, numSamples, 0}; }

    ConstIterator end() const noexcept
    { return {channels, startSample, numSamples, numChannels}; }

    // Yields a Span per channel. The shape is held by value rather than through a
    // pointer back to the Buffer, so an iterator stays valid once the Buffer it
    // came from is gone: `for (auto channel: info.getOutput())` is safe.
    template <typename T>
    class ChannelIterator
    {
    public:
        ChannelIterator(T* const* channelsToUse,
                        int startSampleToUse,
                        int numSamplesToUse,
                        int channelToUse) noexcept
            : channels(channelsToUse)
            , startSample(startSampleToUse)
            , numSamples(numSamplesToUse)
            , channel(channelToUse)
        {
        }

        Span<T> operator*() const noexcept
        { return {channels[channel] + startSample, numSamples}; }

        ChannelIterator& operator++() noexcept
        {
            ++channel;
            return *this;
        }

        bool operator==(const ChannelIterator& other) const noexcept
        { return channel == other.channel; }

        bool operator!=(const ChannelIterator& other) const noexcept
        { return channel != other.channel; }

    private:
        T* const* channels;
        int startSample;
        int numSamples;
        int channel;
    };

private:
    void moveFrom(Buffer& other) noexcept;
    void releaseStorage() noexcept;

    template <typename Operation>
    void forEachSharedChannel(const Buffer& other, Operation operation) noexcept
    {
        auto count = std::min(numChannels, other.numChannels);
        auto length = std::min(numSamples, other.numSamples);

        for (auto channel = 0; channel < count; ++channel)
            operation(getChannel(channel), other.getChannel(channel), length);
    }

    float* const* channels = nullptr;
    int startSample = 0;
    int numChannels = 0;
    int numSamples = 0;

    Vector<float> samples;
    Vector<float*> table;
    bool owning = false;
};

} // namespace MakeASound
