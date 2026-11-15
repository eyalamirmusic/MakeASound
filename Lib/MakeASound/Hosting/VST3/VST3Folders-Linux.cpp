#include "VST3Format.h"
#include "../PathText.h"

#include <eacp/Core/Utils/Environment.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

Vector<std::string> VST3Format::defaultSearchFolders() const
{
    namespace fs = std::filesystem;

    auto folders = Vector<std::string>();

    // Normalised, so "/usr/lib/vst3/" or "~/.vst3" in VST3_PATH is the default.
    auto addIfPresent = [&folders](fs::path folder)
    {
        auto ec = std::error_code();
        folder = folder.lexically_normal();

        if (folder.has_relative_path() && !folder.has_filename())
            folder = folder.parent_path();

        if (!folder.empty() && fs::is_directory(folder, ec))
            folders.addIfNotThere(pathText(folder));
    };

    const auto home = eacp::getEnvValue("HOME");

    // JUCE's convention, ahead of the defaults, with a leading ~ as the home
    // folder (not ~user).
    auto expanded = [&home](std::string_view entry)
    {
        if (entry != "~" && !entry.starts_with("~/"))
            return stdPath(entry);

        if (home.empty())
            return fs::path();

        return stdPath(home + std::string(entry.substr(1)));
    };

    const auto variable = eacp::getEnvValue("VST3_PATH");
    auto path = std::string_view(variable);

    while (!path.empty())
    {
        const auto colon = path.find(':');
        const auto entry = path.substr(0, colon);

        if (!entry.empty())
            addIfPresent(expanded(entry));

        path = colon == std::string_view::npos ? std::string_view()
                                               : path.substr(colon + 1);
    }

    if (!home.empty())
        addIfPresent(stdPath(home) / ".vst3");

    // The spec's order, as the SDK's Module::getModulePaths() walks them.
    addIfPresent(fs::path("/usr/lib64/vst3"));
    addIfPresent(fs::path("/usr/lib/vst3"));
    addIfPresent(fs::path("/usr/local/lib64/vst3"));
    addIfPresent(fs::path("/usr/local/lib/vst3"));

    return folders;
}

} // namespace MakeASound::Hosting
