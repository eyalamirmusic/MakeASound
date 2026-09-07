#include "AudioSession.h"

#import <AVFoundation/AVFoundation.h>

namespace MakeASound
{
namespace
{
AVAudioSessionCategory toCategory(SessionCategory category)
{
    switch (category)
    {
        case SessionCategory::Record:
            return AVAudioSessionCategoryRecord;
        case SessionCategory::PlayAndRecord:
            return AVAudioSessionCategoryPlayAndRecord;
        case SessionCategory::Ambient:
            return AVAudioSessionCategoryAmbient;
        case SessionCategory::Playback:
            break;
    }

    return AVAudioSessionCategoryPlayback;
}

SessionCategory fromCategory(AVAudioSessionCategory category)
{
    if ([category isEqualToString:AVAudioSessionCategoryRecord])
        return SessionCategory::Record;

    if ([category isEqualToString:AVAudioSessionCategoryPlayAndRecord])
        return SessionCategory::PlayAndRecord;

    if ([category isEqualToString:AVAudioSessionCategoryAmbient]
        || [category isEqualToString:AVAudioSessionCategorySoloAmbient])
        return SessionCategory::Ambient;

    return SessionCategory::Playback;
}

bool capturesAudio(SessionCategory category)
{
    return category == SessionCategory::Record
           || category == SessionCategory::PlayAndRecord;
}

AVAudioSessionCategoryOptions toOptions(const SessionOptions& options,
                                        SessionCategory category)
{
    auto flags = AVAudioSessionCategoryOptions {};

    if (options.mixWithOthers)
        flags |= AVAudioSessionCategoryOptionMixWithOthers;

    if (options.duckOthers)
        flags |= AVAudioSessionCategoryOptionDuckOthers;

    if (options.allowBluetooth)
    {
#if defined(__IPHONE_18_0) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_18_0
        flags |= AVAudioSessionCategoryOptionAllowBluetoothHFP;
#else
        flags |= AVAudioSessionCategoryOptionAllowBluetooth;
#endif
        flags |= AVAudioSessionCategoryOptionAllowBluetoothA2DP;
    }

    if (options.allowAirPlay)
        flags |= AVAudioSessionCategoryOptionAllowAirPlay;

    // iOS rejects the whole setCategory call for a category that cannot honour the
    // option, so these two are filtered rather than passed through.
    if (options.defaultToSpeaker && category == SessionCategory::PlayAndRecord)
        flags |= AVAudioSessionCategoryOptionDefaultToSpeaker;

    if (category == SessionCategory::Ambient)
        flags &= ~AVAudioSessionCategoryOptions(
            AVAudioSessionCategoryOptionDuckOthers);

    return flags;
}

SessionOptions fromOptions(AVAudioSessionCategoryOptions flags)
{
    auto options = SessionOptions {};

    auto has = [flags](AVAudioSessionCategoryOptions flag)
    { return (flags & flag) != 0; };

    options.mixWithOthers = has(AVAudioSessionCategoryOptionMixWithOthers);
    options.duckOthers = has(AVAudioSessionCategoryOptionDuckOthers);
    options.defaultToSpeaker = has(AVAudioSessionCategoryOptionDefaultToSpeaker);
    options.allowAirPlay = has(AVAudioSessionCategoryOptionAllowAirPlay);

#if defined(__IPHONE_18_0) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_18_0
    options.allowBluetooth = has(AVAudioSessionCategoryOptionAllowBluetoothHFP)
                             || has(AVAudioSessionCategoryOptionAllowBluetoothA2DP);
#else
    options.allowBluetooth = has(AVAudioSessionCategoryOptionAllowBluetooth)
                             || has(AVAudioSessionCategoryOptionAllowBluetoothA2DP);
#endif

    return options;
}
} // namespace

bool hasAudioSession()
{
    return true;
}

Error applySessionConfig(const SessionConfig& config, bool wantsInput)
{
    @autoreleasepool
    {
        auto* session = [AVAudioSession sharedInstance];

        auto category = config.category.value_or(
            wantsInput ? SessionCategory::PlayAndRecord : SessionCategory::Playback);

        // Asking for a capture side the category cannot carry opens a stream that
        // reads silence, which is harder to diagnose than a refusal.
        if (wantsInput && !capturesAudio(category))
            return Error::INVALID_PARAMETER;

        NSError* error = nil;

        if (![session setCategory:toCategory(category)
                      withOptions:toOptions(config.options, category)
                            error:&error])
            return Error::INVALID_PARAMETER;

        // Preferences, not settings: the route grants what it can and getSessionState
        // reports the result, so a refusal here is not worth failing the open over.
        if (config.preferredSampleRate > 0)
            [session setPreferredSampleRate:config.preferredSampleRate error:nil];

        if (config.preferredBlockSize > 0)
        {
            auto rate = session.sampleRate > 0.0 ? session.sampleRate : 48000.0;
            [session setPreferredIOBufferDuration:config.preferredBlockSize / rate
                                            error:nil];
        }

        if (![session setActive:YES error:&error])
            return Error::DEVICE_DISCONNECT;

        return Error::NoError;
    }
}

Error deactivateSession()
{
    @autoreleasepool
    {
        auto* session = [AVAudioSession sharedInstance];

        // Other apps stay ducked until they are told the session let go.
        auto options =
            AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation;

        return [session setActive:NO withOptions:options error:nil]
                   ? Error::NoError
                   : Error::UNKNOWN_ERROR;
    }
}

SessionState getSessionState()
{
    @autoreleasepool
    {
        auto* session = [AVAudioSession sharedInstance];

        auto state = SessionState {};
        state.available = true;
        state.category = fromCategory(session.category);
        state.options = fromOptions(session.categoryOptions);
        state.outputChannels = static_cast<int>(session.outputNumberOfChannels);
        state.inputChannels = static_cast<int>(session.inputNumberOfChannels);
        state.sampleRate = static_cast<int>(session.sampleRate);
        state.blockSize =
            static_cast<int>(session.IOBufferDuration * session.sampleRate + 0.5);
        state.outputLatencySeconds = session.outputLatency;
        state.inputLatencySeconds = session.inputLatency;

        return state;
    }
}

} // namespace MakeASound
