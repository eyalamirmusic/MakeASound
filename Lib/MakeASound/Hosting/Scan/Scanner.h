#pragma once

#include "../PluginDescription.h"

#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

// The stamp of the file-backed module at `path`.
//
// A plugin module is usually a bundle directory, whose own mtime does not change
// when the binary inside is replaced, and whose contents can be gigabytes of
// samples not worth walking. So a directory stamps from the binaries in its
// architecture folders (Contents/MacOS, Contents/<arch>-win,
// Contents/<arch>-linux): the newest mtime and the total size there. A plain file
// (a bare .vst3 DLL on Windows) stamps as itself, and a directory with no
// Contents as its own mtime.
//
// `lastModified` counts seconds from std::filesystem's own clock epoch, not the
// Unix epoch: clock_cast is missing from some toolchains we build with, and the
// usual workaround of subtracting two "now"s jitters, which would make a stamp
// compare unequal to itself. Never throws; a missing path stamps as {0, 0}.
ModuleStamp moduleStamp(std::string_view path);

// Every entry under `searchPaths` whose name ends with `extension` (".vst3"),
// whether a bundle directory or a plain file.
//
// Symlinks are followed, with loops broken by the canonical path of each folder
// visited. A module is never descended into, since a bundle can hold helper
// bundles that are not loadable on their own. Missing or unreadable folders are
// skipped, and a search path that is itself a module counts as one. The result
// is deduplicated and sorted, so the same tree gives the same list every time.
Vector<std::string> findModulesWithExtension(const Vector<std::string>& searchPaths,
                                             std::string_view extension);

// A "this target never answered" result: `crashed` set, `error` saying how, and
// the target's stamp and format. The stamp matters as much as the message: the
// cache blacklists a failure only while its stamp is unchanged, so a crash
// recorded with a zero stamp would be rescanned, and crash again, every launch.
ModuleScan crashedScan(std::string target,
                       std::string error,
                       ModuleStamp stamp,
                       PluginFormat format);

// The last `maxChars` of what a worker wrote to stderr, as the ": ..." suffix of
// a ModuleScan::error, or empty when it wrote nothing. A plugin that fails says
// why there, but it can also print megabytes, which do not belong in the cache.
std::string stderrTail(const std::string& text, int maxChars = 400);

} // namespace MakeASound::Hosting
