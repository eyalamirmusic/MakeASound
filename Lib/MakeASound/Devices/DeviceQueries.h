#pragma once

#include "../Common/Common.h"
#include "DeviceInfo.h"

namespace MakeASound
{

// Returns the block sizes (audio buffer frame counts) the device can run.
// On macOS the list is queried from the CoreAudio device (matched by name,
// since DeviceInfo::id is a MakeASound-assigned index, not a CoreAudio
// AudioDeviceID), restricted to powers of two between 8 and 4096 that fall
// inside the device's reported frame-size range. Platforms without a
// dedicated implementation — and macOS when the query fails — return the
// conservative 64..2048 fallback.
Vector<int> getSupportedBlockSizes(const DeviceInfo& device);

// The rate the device is running at RIGHT NOW — a different question from which rates
// it supports, or which one we would choose to open it at. Any app can move a shared
// device, so this changes without MakeASound doing anything. Matched by name for the
// same reason as above. Returns 0 on platforms with no dedicated implementation, and
// when the query fails; callers fall back to DeviceInfo::preferredSampleRate.
int getCurrentSampleRate(const DeviceInfo& device);

} // namespace MakeASound
