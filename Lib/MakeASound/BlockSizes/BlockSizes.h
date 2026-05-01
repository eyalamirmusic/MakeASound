#pragma once

#include "../Common/Common.h"

namespace MakeASound
{

// Returns the block sizes (audio buffer frame counts) the device can run.
// The list is restricted to powers of two between 64 and 2048. On macOS
// the list is queried from the CoreAudio device; platforms without a
// dedicated implementation return the full unfiltered fallback.
Vector<int> getSupportedBlockSizes(int deviceId);

} // namespace MakeASound
