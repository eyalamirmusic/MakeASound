#include "Settings.h"

#include <eacp/Core/Utils/FilePath.h>

namespace MakeASound::Standalone
{

// %APPDATA%\<Name>\<Name>.settings
std::string juceSettingsPath(std::string_view juceName)
{
    auto directory = eacp::FilePath::appDataDirectory();

    if (juceName.empty() || directory.str().empty())
        return {};

    auto name = std::string {juceName};
    return (directory / name / (name + ".settings")).str();
}

} // namespace MakeASound::Standalone
