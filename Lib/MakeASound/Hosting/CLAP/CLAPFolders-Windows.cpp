#include "CLAPFormat.h"
#include "CLAPFolders.h"
#include "../PathText.h"

#include <eacp/Core/Utils/Environment.h>

namespace MakeASound::Hosting
{

namespace
{
void addBelow(Vector<std::string>& folders, const char* variable, const char* below)
{
    const auto root = eacp::getEnv(variable);

    if (root && !root->empty())
        CLAPFolders::addIfPresent(folders, stdPath(*root) / stdPath(below));
}
} // namespace

Vector<std::string> CLAPFormat::defaultSearchFolders() const
{
    auto folders = Vector<std::string>();
    CLAPFolders::addSearchPath(folders, ';');

    addBelow(folders, "COMMONPROGRAMFILES", "CLAP");
    addBelow(folders, "LOCALAPPDATA", "Programs/Common/CLAP");

    return folders;
}

} // namespace MakeASound::Hosting
