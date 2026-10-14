#include "VST3Format.h"

#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/StdPath.h>

#include <filesystem>

namespace MakeASound::Hosting
{

Vector<std::string> VST3Format::defaultSearchFolders() const
{
    namespace fs = std::filesystem;

    auto folders = Vector<std::string>();

    // Built through eacp's FilePath, which keeps non-ASCII names intact, and
    // recorded with forward slashes, as the rest of the cache spells paths.
    auto addIfPresent = [&folders](const char* variable, const char* below)
    {
        const auto root = eacp::getEnv(variable);

        if (!root || root->empty())
            return;

        const auto folder = eacp::FilePath(*root) / below;
        auto ec = std::error_code();

        if (fs::is_directory(eacp::toStdPath(folder), ec))
            folders.addIfNotThere(folder.str());
    };

    addIfPresent("COMMONPROGRAMFILES", "VST3");
    addIfPresent("LOCALAPPDATA", "Programs/Common/VST3");

    return folders;
}

} // namespace MakeASound::Hosting
