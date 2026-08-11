#pragma once

#include "../Common/Common.h"
#include "DeviceInfo.h"

namespace MakeASound
{

// Powers of two the device can run. macOS asks CoreAudio, matched by device name
// since DeviceInfo::id is a MakeASound index, not an AudioDeviceID; other platforms,
// and a failed query, return a conservative 64..2048 fallback.
Vector<int> getSupportedBlockSizes(const DeviceInfo& device);

// The rate the device is running at right now — any app can move a shared device, so
// this changes without MakeASound doing anything. Matched by name as above. 0 where
// unavailable; callers fall back to DeviceInfo::preferredSampleRate.
int getCurrentSampleRate(const DeviceInfo& device);

} // namespace MakeASound
