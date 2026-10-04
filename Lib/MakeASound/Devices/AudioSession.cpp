#include "AudioSession.h"

namespace MakeASound
{

std::string getSessionCategoryName(SessionCategory category)
{
    switch (category)
    {
        case SessionCategory::Playback:
            return "Playback";
        case SessionCategory::Record:
            return "Record";
        case SessionCategory::PlayAndRecord:
            return "PlayAndRecord";
        case SessionCategory::Ambient:
            return "Ambient";
    }

    return {};
}

SessionOptions getOptionsForCategory(const SessionOptions& options,
                                     SessionCategory category)
{
    // AVAudioSessionTypes.h, option by option. Output-only categories already route
    // to Bluetooth A2DP and AirPlay and cannot be told so; Bluetooth HFP is a
    // microphone, so Record keeps it.
    auto playsOut = category != SessionCategory::Record;
    auto mixes = category == SessionCategory::Playback
                 || category == SessionCategory::PlayAndRecord;

    auto taken = options;
    taken.mixWithOthers = options.mixWithOthers && mixes;
    taken.duckOthers =
        options.duckOthers && playsOut && category != SessionCategory::Ambient;
    taken.defaultToSpeaker =
        options.defaultToSpeaker && category == SessionCategory::PlayAndRecord;
    taken.allowBluetooth = options.allowBluetooth
                           && (category == SessionCategory::PlayAndRecord
                               || category == SessionCategory::Record);
    taken.allowAirPlay =
        options.allowAirPlay && category == SessionCategory::PlayAndRecord;

    return taken;
}

} // namespace MakeASound
