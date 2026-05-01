#include "BlockSizes.h"

#include <CoreAudio/CoreAudio.h>

#include <vector>

namespace MakeASound
{

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
        return sizes;

    auto ranges = std::vector<AudioValueRange>(byteSize / sizeof(AudioValueRange));

    err = AudioObjectGetPropertyData(static_cast<AudioObjectID>(deviceId),
                                     &address,
                                     0,
                                     nullptr,
                                     &byteSize,
                                     ranges.data());

    if (err != noErr)
        return sizes;

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

    return sizes;
}

} // namespace MakeASound
