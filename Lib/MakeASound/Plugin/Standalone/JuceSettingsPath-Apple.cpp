#include "Settings.h"

#include <eacp/Core/Utils/FilePath.h>

namespace MakeASound::Standalone
{

// ~/Library/Application Support/<Name>.settings, the app's own container on iOS.
std::string juceSettingsPath(std::string_view juceName)
{
    auto directory = eacp::FilePath::appDataDirectory();

    if (juceName.empty() || directory.str().empty())
        return {};

    return (directory / (std::string {juceName} + ".settings")).str();
}

} // namespace MakeASound::Standalone
