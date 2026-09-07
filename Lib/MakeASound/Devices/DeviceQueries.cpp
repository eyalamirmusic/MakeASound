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

int getRouteLatency(const DeviceInfo& /*device*/, bool /*input*/)
{
    // Nothing portable to ask; the stream reports its own buffering and no more.
    return 0;
}

std::string getDefaultDeviceName(bool /*input*/)
{
    // Nothing portable to ask; the backend's own isDefault flags stand.
    return {};
}

int getCurrentSampleRate(const DeviceInfo& /*device*/)
{
    // Nothing portable to ask; 0 sends the caller to its fallback.
    return 0;
}

std::optional<NativeFormat> getNativeFormat(bool /*input*/)
{
    return std::nullopt;
}

} // namespace MakeASound
