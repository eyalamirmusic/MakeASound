#include "BlockSizes.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <optional>
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

// CoreAudio device IDs (AudioObjectID values) are not the same as the
// sequential indices MakeASound exposes via DeviceInfo::id (those come
// from RtAudio's internal `currentDeviceId_++`). We bridge the two by
// enumerating CoreAudio devices ourselves and matching against the name
// RtAudio already reported.

std::vector<AudioObjectID> coreAudioDeviceIds()
{
    auto address = AudioObjectPropertyAddress {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain};

    auto byteSize = UInt32 {};
    auto err = AudioObjectGetPropertyDataSize(
        kAudioObjectSystemObject, &address, 0, nullptr, &byteSize);

    if (err != noErr || byteSize == 0)
        return {};

    auto ids = std::vector<AudioObjectID>(byteSize / sizeof(AudioObjectID));
    err = AudioObjectGetPropertyData(
        kAudioObjectSystemObject, &address, 0, nullptr, &byteSize, ids.data());

    if (err != noErr)
        return {};

    return ids;
}

std::optional<std::string> coreAudioStringProperty(AudioObjectID id,
                                                   AudioObjectPropertySelector selector)
{
    auto address = AudioObjectPropertyAddress {
        selector,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain};

    auto value = CFStringRef {};
    auto byteSize = static_cast<UInt32>(sizeof(value));
    auto err =
        AudioObjectGetPropertyData(id, &address, 0, nullptr, &byteSize, &value);

    if (err != noErr || value == nullptr)
        return std::nullopt;

    auto length = CFStringGetLength(value);
    auto maxBytes =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    auto buffer = std::vector<char>(static_cast<std::size_t>(maxBytes), '\0');

    auto ok =
        CFStringGetCString(value, buffer.data(), maxBytes, kCFStringEncodingUTF8);
    CFRelease(value);

    if (!ok)
        return std::nullopt;

    return std::string {buffer.data()};
}

// Mirrors RtAudio's macOS naming: "<Manufacturer>: <Name>".
std::optional<std::string> coreAudioCompositeName(AudioObjectID id)
{
    auto manufacturer =
        coreAudioStringProperty(id, kAudioObjectPropertyManufacturer);
    auto name = coreAudioStringProperty(id, kAudioObjectPropertyName);

    if (!manufacturer || !name)
        return std::nullopt;

    return *manufacturer + ": " + *name;
}

std::optional<AudioObjectID> findCoreAudioDevice(const std::string& name)
{
    for (auto id: coreAudioDeviceIds())
    {
        auto composite = coreAudioCompositeName(id);
        if (composite && *composite == name)
            return id;
    }

    return std::nullopt;
}

Vector<int> queryFrameSizes(AudioObjectID id)
{
    auto address = AudioObjectPropertyAddress {
        kAudioDevicePropertyBufferFrameSizeRange,
        kAudioObjectPropertyScopeWildcard,
        kAudioObjectPropertyElementMain};

    auto byteSize = UInt32 {};
    auto err = AudioObjectGetPropertyDataSize(id, &address, 0, nullptr, &byteSize);

    if (err != noErr || byteSize == 0)
        return {};

    auto ranges = std::vector<AudioValueRange>(byteSize / sizeof(AudioValueRange));

    err = AudioObjectGetPropertyData(
        id, &address, 0, nullptr, &byteSize, ranges.data());

    if (err != noErr)
        return {};

    auto sizes = Vector<int>();

    for (auto candidate = 8; candidate <= 4096; candidate *= 2)
    {
        for (auto& range: ranges)
        {
            auto cand = static_cast<double>(candidate);
            if (cand >= range.mMinimum && cand <= range.mMaximum)
            {
                sizes.add(candidate);
                break;
            }
        }
    }

    return sizes;
}
} // namespace

Vector<int> getSupportedBlockSizes(const DeviceInfo& device)
{
    auto coreAudioId = findCoreAudioDevice(device.name);

    if (!coreAudioId)
        return defaultBlockSizes();

    auto sizes = queryFrameSizes(*coreAudioId);

    if (sizes.empty())
        return defaultBlockSizes();

    return sizes;
}

} // namespace MakeASound
