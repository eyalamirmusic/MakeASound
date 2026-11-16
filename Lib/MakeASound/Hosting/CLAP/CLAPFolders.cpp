#include "CLAPFolders.h"
#include "../PathText.h"

#include <eacp/Core/Utils/Environment.h>

namespace MakeASound::Hosting::CLAPFolders
{

void addIfPresent(Vector<std::string>& folders, const std::filesystem::path& folder)
{
    auto normal = folder.lexically_normal();

    if (normal.has_relative_path() && !normal.has_filename())
        normal = normal.parent_path();

    auto ec = std::error_code();

    if (!normal.empty() && std::filesystem::is_directory(normal, ec))
        folders.addIfNotThere(pathText(normal));
}

void addSearchPath(Vector<std::string>& folders, char separator)
{
    const auto variable = eacp::getEnvValue("CLAP_PATH");
    auto path = std::string_view(variable);

    while (!path.empty())
    {
        const auto end = path.find(separator);
        const auto entry = path.substr(0, end);

        if (!entry.empty())
            addIfPresent(folders, stdPath(entry));

        path = end == std::string_view::npos ? std::string_view()
                                             : path.substr(end + 1);
    }
}

} // namespace MakeASound::Hosting::CLAPFolders
