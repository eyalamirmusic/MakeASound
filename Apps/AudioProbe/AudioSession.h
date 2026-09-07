#pragma once

#include <string>

namespace AudioProbe
{

// What the platform's audio session says about itself. Only iOS has one, so
// everywhere else this reports available = false and the session probes stand
// down.
struct SessionState
{
    bool available = false;

    std::string category;
    std::string options;

    // Zero until the session has been activated, which is what makes these the
    // readable answer to "did something activate it behind my back".
    int outputChannels = 0;
    int inputChannels = 0;

    double sampleRate = 0.0;
    double ioBufferSeconds = 0.0;
    double outputLatencySeconds = 0.0;
    double inputLatencySeconds = 0.0;

    // Whether the bundle carries NSMicrophoneUsageDescription. An iOS app that
    // opens a capture side without it is terminated by the OS.
    bool micUsageDescription = false;
};

SessionState snapshotSession();

inline std::string describe(const SessionState& state)
{
    if (!state.available)
        return "no audio session on this platform";

    auto options =
        state.options.empty() ? std::string {"no options"} : state.options;

    return state.category + " [" + options + "], out "
           + std::to_string(state.outputChannels) + "ch, in "
           + std::to_string(state.inputChannels) + "ch";
}

} // namespace AudioProbe
