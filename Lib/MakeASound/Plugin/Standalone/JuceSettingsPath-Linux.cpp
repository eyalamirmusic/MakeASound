#include "Settings.h"

#include <eacp/Core/Utils/FilePath.h>

namespace MakeASound::Standalone
{

// ~/.config/<Name>.settings: JUCE names the folder itself, whatever XDG says.
std::string juceSettingsPath(std::string_view juceName)
{
    auto home = eacp::FilePath::homeDirectory();

    if (juceName.empty() || home.str().empty())
        return {};

    return (home / ".config" / (std::string {juceName} + ".settings")).str();
}

} // namespace MakeASound::Standalone
