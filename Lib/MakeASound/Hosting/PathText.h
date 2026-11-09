#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

// How hosting turns a std::filesystem::path into the text a PluginCache stores,
// and that text back into a path. The text is UTF-8 on every platform.
// std::filesystem::path::string() is not: on Windows it encodes in the ANSI code
// page and throws for a character that code page lacks, and a path built from a
// narrow string decodes it in that code page, so a plugin under a non-ASCII
// folder or user name could be neither found nor stamped. All three never throw.

// UTF-8 with '/' separators on every platform, Windows included: the spelling
// the cache uses for every path it records (eacp::FilePath::str()).
std::string pathText(const std::filesystem::path& path);

// The path a UTF-8 text names. On Windows '\' and '/' both separate, so text
// read from a cache an older build wrote, or typed by a user, works as well.
std::filesystem::path stdPath(std::string_view text);

// The form two texts are compared in to decide whether they name the same
// target. On Windows, where file names ignore case, '\' reads as '/', case is
// folded as NTFS folds it (per UTF-16 unit in the invariant locale, so beyond
// ASCII but not beyond the BMP; text that is not valid UTF-8 in its ASCII bytes
// only), repeated and trailing separators read as one and none, and a "\\?\X:"
// or "\\?\UNC\" prefix is dropped, so "c:\vst3\a.vst3", "C:\VST3\\A.vst3\",
// "\\?\C:\VST3\A.vst3" and "C:/VST3/A.vst3" are one cache entry. A UNC path
// keeps its leading "//" and a drive's root its '/', and a key's key is itself.
// Not folded: "..", the "\\.\" device prefix, "\\?\C:" (the volume, which keys
// as "C:"), 8.3 short names, and the trailing dots and spaces Win32 strips.
// A folder made case-sensitive (fsutil setCaseSensitiveInfo, as WSL does) can
// hold two modules whose names differ only in case; they share a key, and the
// cache sees one. Elsewhere the key is the text itself, byte for byte, so case
// and separators keep two texts apart there, and the text of a target that is
// not a file (an Audio Unit's "aufx:mrev:appl") is its own key. One TU per
// platform: PathText-Windows.cpp, PathText-macOS.cpp.
std::string pathKey(std::string_view text);

} // namespace MakeASound::Hosting
