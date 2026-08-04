#include "DeviceQueries.h"

namespace MakeASound
{

Vector<int> getSupportedBlockSizes(const DeviceInfo& /*device*/)
{
    auto sizes = Vector<int>();

    for (auto size = 64; size <= 2048; size *= 2)
        sizes.add(size);

    return sizes;
}

int getCurrentSampleRate(const DeviceInfo& /*device*/)
{
    // Nothing portable to ask. 0 sends the caller to its fallback rather than having it
    // trust a guess that would be wrong the moment another app moved the device.
    return 0;
}

} // namespace MakeASound
