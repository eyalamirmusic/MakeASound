#pragma once

#include "../Common/Common.h"
#include "../DeviceInfo/DeviceInfo.h"

namespace MakeASound
{

// Returns the block sizes (audio buffer frame counts) the device can run.
// On macOS the list is queried from the CoreAudio device (matched by name,
// since DeviceInfo::id is RtAudio's internal sequential index, not a
// CoreAudio AudioDeviceID), restricted to powers of two between 8 and 4096
// that fall inside the device's reported frame-size range. Platforms
// without a dedicated implementation — and macOS when the query fails —
// return the conservative 64..2048 fallback.
Vector<int> getSupportedBlockSizes(const DeviceInfo& device);

} // namespace MakeASound
