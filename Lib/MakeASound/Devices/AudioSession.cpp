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

} // namespace MakeASound
