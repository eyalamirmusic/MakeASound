#pragma once

#include "../../Common/Common.h"

#include <filesystem>
#include <string>
#include <string_view>

// What every platform's CLAP default folders share (CLAPFolders-<os>.cpp).
namespace MakeASound::Hosting::CLAPFolders
{

// Adds `folder` when it is a directory, normalised (no trailing separator, no
// "." or ".." left) and spelled as pathText spells it, unless it is there.
void addIfPresent(Vector<std::string>& folders, const std::filesystem::path& folder);

// Adds each entry of $CLAP_PATH that is a directory, in order, the entries split
// at `separator` (':', or ';' on Windows). Empty entries are skipped.
void addSearchPath(Vector<std::string>& folders, char separator);

} // namespace MakeASound::Hosting::CLAPFolders
