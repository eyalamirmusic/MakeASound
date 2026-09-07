#include "AudioSession.h"

#import <AVFoundation/AVFoundation.h>

namespace AudioProbe
{
namespace
{
std::string toString(NSString* string)
{
    return string == nil ? std::string {} : std::string {[string UTF8String]};
}

std::string describeOptions(AVAudioSessionCategoryOptions options)
{
    auto names = std::string {};

    auto add = [&](AVAudioSessionCategoryOptions flag, const char* name)
    {
        if ((options & flag) == 0)
            return;

        if (!names.empty())
            names += " | ";

        names += name;
    };

    add(AVAudioSessionCategoryOptionMixWithOthers, "MixWithOthers");
    add(AVAudioSessionCategoryOptionDuckOthers, "DuckOthers");
#if defined(__IPHONE_18_0) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_18_0
    add(AVAudioSessionCategoryOptionAllowBluetoothHFP, "AllowBluetoothHFP");
#else
    add(AVAudioSessionCategoryOptionAllowBluetooth, "AllowBluetooth");
#endif
    add(AVAudioSessionCategoryOptionDefaultToSpeaker, "DefaultToSpeaker");
    add(AVAudioSessionCategoryOptionAllowBluetoothA2DP, "AllowBluetoothA2DP");
    add(AVAudioSessionCategoryOptionAllowAirPlay, "AllowAirPlay");

    return names;
}
} // namespace

SessionState snapshotSession()
{
    @autoreleasepool
    {
        auto* session = [AVAudioSession sharedInstance];
        auto* bundle = [NSBundle mainBundle];

        auto state = SessionState {};
        state.available = true;
        state.category = toString(session.category);
        state.options = describeOptions(session.categoryOptions);
        state.outputChannels = static_cast<int>(session.outputNumberOfChannels);
        state.inputChannels = static_cast<int>(session.inputNumberOfChannels);
        state.sampleRate = session.sampleRate;
        state.ioBufferSeconds = session.IOBufferDuration;
        state.outputLatencySeconds = session.outputLatency;
        state.inputLatencySeconds = session.inputLatency;
        state.micUsageDescription =
            [bundle objectForInfoDictionaryKey:@"NSMicrophoneUsageDescription"]
            != nil;

        return state;
    }
}

} // namespace AudioProbe
