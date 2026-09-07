#include "AudioSession.h"

namespace MakeASound
{

bool hasAudioSession()
{
    return false;
}

Error applySessionConfig(const SessionConfig&, bool)
{
    return Error::NoError;
}

Error deactivateSession()
{
    return Error::NoError;
}

SessionState getSessionState()
{
    return {};
}

} // namespace MakeASound
