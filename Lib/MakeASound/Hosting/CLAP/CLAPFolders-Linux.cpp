#include "CLAPFormat.h"
#include "CLAPFolders.h"
#include "../PathText.h"

#include <eacp/Core/Utils/Environment.h>

namespace MakeASound::Hosting
{

Vector<std::string> CLAPFormat::defaultSearchFolders() const
{
    auto folders = Vector<std::string>();
    CLAPFolders::addSearchPath(folders, ':');

    if (const auto home = eacp::getEnv("HOME"); home && !home->empty())
        CLAPFolders::addIfPresent(folders, stdPath(*home) / ".clap");

    CLAPFolders::addIfPresent(folders, "/usr/lib/clap");

    return folders;
}

} // namespace MakeASound::Hosting
