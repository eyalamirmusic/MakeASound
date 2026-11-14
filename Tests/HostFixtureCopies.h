#pragma once

// A fixture plugin copied under a folder whose name is not ASCII, for the cases
// that scan and load from such a path. The folder is U+00DC n U+00EF c U+00F8 d
// U+00E9 - U+65E5 U+672C: Latin-1 letters every Western code page has, and two
// CJK ones a Western code page lacks.

#include <filesystem>
#include <string>
#include <system_error>

namespace MakeASound::HostFixtureCopies
{
inline std::filesystem::path nonAsciiFolderName()
{
    return std::filesystem::path(u8"\u00dcn\u00efc\u00f8d\u00e9-\u65e5\u672c");
}

inline std::string utf8Text(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

// Copies <fixtureFolder>/<name>.vst3 into <root>/<non-ASCII folder>/, after
// removing whatever an earlier run left in root, and returns the copy's path as
// UTF-8. A module a previous run still had loaded may keep a file locked, so the
// removal is best effort, and a copy that fails returns an empty path, which the
// case's own checks then report, rather than throwing out of it.
inline std::string copyIntoNonAsciiFolder(const std::filesystem::path& fixtureFolder,
                                          const std::string& name,
                                          const std::filesystem::path& root)
{
    namespace fs = std::filesystem;

    auto error = std::error_code();
    fs::remove_all(root, error);

    const auto folder = root / nonAsciiFolderName();
    const auto bundle = folder / (name + ".vst3");
    error.clear();
    fs::create_directories(folder, error);
    fs::copy(fixtureFolder / (name + ".vst3"),
             bundle,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             error);

    return error ? std::string() : utf8Text(bundle);
}
} // namespace MakeASound::HostFixtureCopies
