#pragma once

#include <MakeASound/MakeASound.h>

#include <string>

namespace AudioProbe
{
namespace MS = MakeASound;

// The one thing about the session that MakeASound cannot answer: whether this
// bundle carries NSMicrophoneUsageDescription. An iOS app that opens a capture
// side without it is terminated by the OS. Everything else comes from
// MS::getSessionState().
bool hasMicUsageDescription();

inline std::string describe(const MS::SessionState& state)
{
    if (!state.available)
        return "no audio session on this platform";

    return MS::getSessionCategoryName(state.category) + ", out "
           + std::to_string(state.outputChannels) + "ch, in "
           + std::to_string(state.inputChannels) + "ch, "
           + std::to_string(state.sampleRate) + " Hz, block "
           + std::to_string(state.blockSize);
}

} // namespace AudioProbe
