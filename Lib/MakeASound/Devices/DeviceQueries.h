#pragma once

#include "../Common/Common.h"
#include "DeviceInfo.h"

#include <optional>

namespace MakeASound
{

// What a device runs at, where the platform can say without opening anything.
struct NativeFormat
{
    int channels {};
    int sampleRate {};
};

// iOS answers from AVAudioSession — one route, one format — which keeps enumeration
// away from CoreAudio's RemoteIO unit, whose RPC to the audio daemon aborts the
// process when it times out. nullopt elsewhere: the backend's own query is safe.
std::optional<NativeFormat> getNativeFormat(bool input);

// Powers of two the device can run. macOS asks CoreAudio, matched by device name
// since DeviceInfo::id is a MakeASound index, not an AudioDeviceID; other platforms,
// and a failed query, return a conservative 64..2048 fallback.
Vector<int> getSupportedBlockSizes(const DeviceInfo& device);

// The rate the device is running at right now — any app can move a shared device, so
// this changes without MakeASound doing anything. Matched by name as above. 0 where
// unavailable; callers fall back to DeviceInfo::preferredSampleRate.
int getCurrentSampleRate(const DeviceInfo& device);

} // namespace MakeASound
