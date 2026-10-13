#include "Settings.h"

namespace MakeASound::Standalone
{

// No JUCE standalone this knows the settings of.
std::string juceSettingsPath(std::string_view)
{
    return {};
}

} // namespace MakeASound::Standalone
