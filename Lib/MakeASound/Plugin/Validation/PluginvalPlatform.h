#pragma once

/** @file
    The per-platform parts of fetching and running pluginval. What differs per
    platform, one source file each: Pluginval-macOS/-Windows/-Linux.cpp.
*/

#include <eacp/Core/Utils/FilePath.h>

#include <string>

namespace MakeASound::Pluginval
{
/// Where this platform's pluginval build is in Tracktion's release.
struct Asset
{
    /// The release asset's file name, and the binary's path inside the unpacked zip.
    std::string archive;
    std::string binary;
};

/// Throws std::runtime_error where Tracktion ships no build.
Asset platformAsset();

/// eacp's zip reader drops the mode bits, so a POSIX binary arrives unexecutable.
void makeExecutable(const eacp::FilePath& binary);
} // namespace MakeASound::Pluginval
