#include "BlockSizes.h"

#include <CoreAudio/CoreAudio.h>

#include <vector>

namespace MakeASound
{

namespace
{
Vector<int> defaultBlockSizes()
{
    auto sizes = Vector<int>();

    for (auto size = 64; size <= 2048; size *= 2)
        sizes.add(size);

    return sizes;
}
} // namespace

// `deviceId` is the value MakeASound exposes via DeviceInfo::id, which is
// RtAudio's internal sequential index — not a CoreAudio AudioDeviceID. We
// still attempt the property query (in case the IDs happen to coincide,
// e.g. on a future backend that exposes raw AudioDeviceIDs) and fall back
// to the universal power-of-two range otherwise so the dropdown always
// has selectable values.
Vector<int> getSupportedBlockSizes(int deviceId)
{
    auto sizes = Vector<int>();

    auto address = AudioObjectPropertyAddress {
        kAudioDevicePropertyBufferFrameSizeRange,
        kAudioObjectPropertyScopeWildcard,
        kAudioObjectPropertyElementMain};

    auto byteSize = UInt32 {};
    auto err = AudioObjectGetPropertyDataSize(
        static_cast<AudioObjectID>(deviceId), &address, 0, nullptr, &byteSize);

    if (err != noErr || byteSize == 0)
        return defaultBlockSizes();

    auto ranges = std::vector<AudioValueRange>(byteSize / sizeof(AudioValueRange));

    err = AudioObjectGetPropertyData(static_cast<AudioObjectID>(deviceId),
                                     &address,
                                     0,
                                     nullptr,
                                     &byteSize,
                                     ranges.data());

    if (err != noErr)
        return defaultBlockSizes();

    for (auto candidate = 16; candidate <= 4096; candidate *= 2)
    {
        for (auto& range: ranges)
        {
            auto cand = static_cast<double>(candidate);
            if (cand >= range.mMinimum
                && cand <= range.mMaximum)
            {
                sizes.add(candidate);
                break;
            }
        }
    }

    if (sizes.empty())
        return defaultBlockSizes();

    return sizes;
}

} // namespace MakeASound
