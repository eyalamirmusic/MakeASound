#include "DeviceQueries.h"

#import <AVFoundation/AVFoundation.h>

namespace MakeASound
{

Vector<int> getSupportedBlockSizes(const DeviceInfo& /*device*/)
{
    // iOS takes a preferred IO duration rather than a frame count, and grants
    // whatever the route allows, so there is no list to read back.
    auto sizes = Vector<int>();

    for (auto size = 64; size <= 2048; size *= 2)
        sizes.add(size);

    return sizes;
}

int getCurrentSampleRate(const DeviceInfo& /*device*/)
{
    @autoreleasepool
    {
        return static_cast<int>([AVAudioSession sharedInstance].sampleRate);
    }
}

std::optional<NativeFormat> getNativeFormat(bool input)
{
    @autoreleasepool
    {
        auto* session = [AVAudioSession sharedInstance];

        auto format = NativeFormat {};
        format.sampleRate = static_cast<int>(session.sampleRate);

        // The maximum is the route's own channel count; the current one is only as
        // wide as the session has been configured, and is 0 before it is activated.
        format.channels = static_cast<int>(
            input ? session.maximumInputNumberOfChannels
                  : session.maximumOutputNumberOfChannels);

        if (format.channels == 0)
            format.channels =
                static_cast<int>(input ? session.inputNumberOfChannels
                                       : session.outputNumberOfChannels);

        return format;
    }
}

} // namespace MakeASound
