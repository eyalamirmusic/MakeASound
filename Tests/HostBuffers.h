#pragma once

// What a host hands an audio unit, shared by the AU and AUv3 suites: one bus of
// host memory with the AudioBufferList that points at it, and the MIDI the unit
// sends back, kept in fixed storage so collecting it never allocates.

#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <vector>

namespace HostBuffers
{

// What a host buffer can hold, past any block a case renders.
constexpr auto bufferCapacity = 1024;

// A packet the unit sent, kept in fixed storage so collecting it never allocates.
struct SentMidi
{
    UInt32 bus = 0;
    MIDITimeStamp offset = 0;
    UInt16 length = 0;
    std::array<Byte, 64> bytes {};

    bool is(std::initializer_list<int> expected) const
    {
        return std::equal(
            bytes.begin(), bytes.begin() + length, expected.begin(), expected.end());
    }
};

// One bus of host memory and the AudioBufferList a host hands over.
struct HostBus
{
    explicit HostBus(int numChannels)
        : storage(static_cast<size_t>(numChannels),
                  std::vector<float>(bufferCapacity))
        , listStorage(offsetof(AudioBufferList, mBuffers)
                      + std::max(1, numChannels) * sizeof(AudioBuffer))
    {
    }

    int numChannels() const { return static_cast<int>(storage.size()); }

    void fill(float value)
    {
        for (auto& channel: storage)
            std::fill(channel.begin(), channel.end(), value);
    }

    // Null data asks the unit for its own buffers, which it hands back.
    AudioBufferList& list(UInt32 frames, bool hostMemory = true)
    {
        auto& result = *reinterpret_cast<AudioBufferList*>(listStorage.data());
        result.mNumberBuffers = static_cast<UInt32>(storage.size());

        for (auto ch = 0; ch < numChannels(); ++ch)
            result.mBuffers[ch] = {
                1,
                static_cast<UInt32>(frames * sizeof(float)),
                hostMemory ? storage[static_cast<size_t>(ch)].data() : nullptr};

        return result;
    }

    std::vector<std::vector<float>> storage;
    std::vector<std::byte> listStorage;
};

} // namespace HostBuffers
