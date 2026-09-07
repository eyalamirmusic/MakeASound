#include "DeviceQueries.h"

#import <AVFoundation/AVFoundation.h>

namespace MakeASound
{
namespace
{
// What setPreferredSampleRate: is worth asking for. iOS grants the nearest rate the
// route supports rather than refusing, so this is a menu, not a guarantee — the
// stream reports what actually happened.
Vector<int> askableSampleRates(int current)
{
    auto rates = Vector<int> {8000, 11025, 16000, 22050, 32000, 44100, 48000};

    if (current > 0)
        rates.addIfNotThere(current);

    rates.sort();

    return rates;
}
} // namespace

Vector<int> getSupportedBlockSizes(const DeviceInfo& /*device*/)
{
    // iOS takes a preferred IO duration rather than a frame count and grants what the
    // route allows, so this is what setPreferredIOBufferDuration: is worth asking
    // for; AudioCallbackInfo::maxBlockSize says what was granted.
    auto sizes = Vector<int>();

    for (auto size = 64; size <= 2048; size *= 2)
        sizes.add(size);

    return sizes;
}

int getRouteLatency(const DeviceInfo& /*device*/, bool input)
{
    @autoreleasepool
    {
        auto* session = [AVAudioSession sharedInstance];
        auto seconds = input ? session.inputLatency : session.outputLatency;

        return static_cast<int>(seconds * session.sampleRate);
    }
}

std::string getDefaultDeviceName(bool /*input*/)
{
    // There is one route and it is the default one; enumeration flags it already.
    return {};
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
        format.sampleRates = askableSampleRates(format.sampleRate);

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
