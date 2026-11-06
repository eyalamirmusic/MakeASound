#include "VST3Format.h"

#include <eacp/Core/Utils/Environment.h>

#include <filesystem>

namespace MakeASound::Hosting
{

Vector<std::string> VST3Format::defaultSearchFolders() const
{
    namespace fs = std::filesystem;

    auto folders = Vector<std::string>();

    auto addIfPresent = [&folders](const fs::path& folder)
    {
        auto ec = std::error_code();

        if (!folder.empty() && fs::is_directory(folder, ec))
            folders.addIfNotThere(folder.generic_string());
    };

    if (const auto home = eacp::getEnv("HOME"); home && !home->empty())
        addIfPresent(fs::path(*home) / "Library" / "Audio" / "Plug-Ins" / "VST3");

    addIfPresent(fs::path("/Library/Audio/Plug-Ins/VST3"));

    return folders;
}

} // namespace MakeASound::Hosting
