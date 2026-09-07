#pragma once

#include <Miro/Miro.h>

#include "../Common/Common.h"
#include "DeviceInfo.h"

#include <optional>
#include <string>

namespace MakeASound
{

// What the platform's audio session lets an app be. Only iOS has one; everywhere
// else hasAudioSession() is false and the calls below do nothing.
enum class SessionCategory
{
    // Output only, still audible with the ring switch silent. No microphone, so no
    // usage description and no recording indicator.
    Playback,

    Record,

    // The only category that can capture, and the reason a duplex app needs
    // NSMicrophoneUsageDescription.
    PlayAndRecord,

    // Output only, mixed with whatever else is playing and silenced by the switch.
    Ambient
};

std::string getSessionCategoryName(SessionCategory category);

struct SessionOptions
{
    MIRO_REFLECT(mixWithOthers,
                 duckOthers,
                 defaultToSpeaker,
                 allowBluetooth,
                 allowAirPlay)

    // Leave other apps playing. Off, opening a stream stops the user's music.
    bool mixWithOthers = false;
    bool duckOthers = false;

    // PlayAndRecord routes to the receiver unless this is set, which sounds like a
    // broken phone.
    bool defaultToSpeaker = true;

    bool allowBluetooth = true;
    bool allowAirPlay = true;
};

struct SessionConfig
{
    MIRO_REFLECT(category, options, preferredSampleRate, preferredBlockSize)

    // Unset follows the stream: Playback with no input side, PlayAndRecord with one.
    std::optional<SessionCategory> category;

    SessionOptions options;

    // What to ask the route for. 0 follows the stream — the rate and block size the
    // StreamConfig asked for. The route grants what it can either way, and
    // getSessionState() says what that was.
    int preferredSampleRate = 0;
    int preferredBlockSize = 0;
};

// What the session is doing now, as opposed to what was asked of it.
struct SessionState
{
    MIRO_REFLECT(available,
                 category,
                 options,
                 outputChannels,
                 inputChannels,
                 sampleRate,
                 blockSize,
                 outputLatencySeconds,
                 inputLatencySeconds)

    bool available = false;

    SessionCategory category = SessionCategory::Playback;
    SessionOptions options;

    int outputChannels = 0;
    int inputChannels = 0;
    int sampleRate = 0;
    int blockSize = 0;

    // The route's own latency, on top of whatever the stream reports.
    double outputLatencySeconds = 0.0;
    double inputLatencySeconds = 0.0;
};

bool hasAudioSession();

// Resolves an unset category from `wantsInput`, applies the rest and activates.
// NoError where there is no session to apply it to.
Error applySessionConfig(const SessionConfig& config, bool wantsInput);

Error deactivateSession();

SessionState getSessionState();

} // namespace MakeASound
