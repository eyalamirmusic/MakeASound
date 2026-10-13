#include "Scanner.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>
#include <utility>
#include <vector>

namespace MakeASound::Hosting
{

namespace
{
namespace fs = std::filesystem;

// Deep enough for any real plugin folder, shallow enough that a pathological
// layout cannot turn the walk into a hang. The visited set already breaks
// symlink loops; this is the second line.
constexpr auto maxWalkDepth = 24;

std::int64_t toStampSeconds(fs::file_time_type time)
{
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch())
        .count();
}

// The folders inside a bundle that hold the loadable binary, per platform layout.
// Named rather than globbed, so a bundle full of resources costs a few stats.
bool isArchitectureFolder(const std::string& name)
{
    if (name == "MacOS")
        return true;

    return name.find("-win") != std::string::npos
           || name.find("-linux") != std::string::npos;
}

void accumulateFile(const fs::path& file, ModuleStamp& stamp)
{
    auto ec = std::error_code();

    if (const auto size = fs::file_size(file, ec); !ec)
        stamp.fileSize += static_cast<std::int64_t>(size);

    ec.clear();

    if (const auto time = fs::last_write_time(file, ec); !ec)
        stamp.lastModified = std::max(stamp.lastModified, toStampSeconds(time));
}
} // namespace

ModuleStamp moduleStamp(std::string_view path)
{
    auto stamp = ModuleStamp();
    auto ec = std::error_code();
    const auto root = fs::path(std::string(path));

    if (path.empty() || !fs::exists(root, ec) || ec)
        return stamp;

    if (fs::is_regular_file(root, ec) && !ec)
    {
        accumulateFile(root, stamp);
        return stamp;
    }

    ec.clear();
    const auto contents = root / "Contents";

    if (!fs::is_directory(contents, ec) || ec)
    {
        // Not a bundle we recognise: the folder's own mtime is still a stamp.
        ec.clear();

        if (const auto time = fs::last_write_time(root, ec); !ec)
            stamp.lastModified = toStampSeconds(time);

        return stamp;
    }

    ec.clear();

    for (const auto& entry: fs::directory_iterator(contents, ec))
    {
        auto entryError = std::error_code();

        if (!entry.is_directory(entryError) || entryError)
            continue;

        if (!isArchitectureFolder(entry.path().filename().string()))
            continue;

        auto binaryError = std::error_code();

        for (const auto& binary: fs::directory_iterator(entry.path(), binaryError))
        {
            auto fileError = std::error_code();

            if (binary.is_regular_file(fileError) && !fileError)
                accumulateFile(binary.path(), stamp);
        }
    }

    return stamp;
}

Vector<std::string> findModulesWithExtension(const Vector<std::string>& searchPaths,
                                             std::string_view extension)
{
    // A set both deduplicates (search paths can overlap, or meet through a
    // symlink) and sorts.
    auto found = std::set<std::string>();
    auto visited = std::set<std::string>();
    auto pending = std::vector<std::pair<fs::path, int>>();

    const auto isModule = [&](const fs::path& path)
    { return Text::endsWithIgnoringCase(path.filename().string(), extension); };

    // Canonical form, only to break loops. A path the filesystem cannot
    // canonicalise is taken as given: a duplicate visit is cheaper than a hang.
    const auto identityOf = [](const fs::path& path)
    {
        auto ec = std::error_code();
        const auto canonical = fs::canonical(path, ec);

        return ec ? path.string() : canonical.string();
    };

    for (const auto& searchPath: searchPaths)
    {
        if (searchPath.empty())
            continue;

        auto root = fs::path(searchPath);
        auto ec = std::error_code();

        if (!fs::exists(root, ec) || ec)
            continue;

        if (isModule(root))
            found.insert(root.string());
        else if (fs::is_directory(root, ec) && !ec)
            pending.emplace_back(std::move(root), 0);
    }

    while (!pending.empty())
    {
        const auto [directory, depth] = pending.back();
        pending.pop_back();

        if (!visited.insert(identityOf(directory)).second)
            continue;

        auto ec = std::error_code();

        // The error_code overload turns an unreadable folder into an empty range,
        // so one locked folder never aborts the sweep of the others.
        for (const auto& entry: fs::directory_iterator(directory, ec))
        {
            const auto& entryPath = entry.path();

            if (isModule(entryPath))
            {
                found.insert(entryPath.string());
                continue;
            }

            auto entryError = std::error_code();

            // is_directory resolves symlinks, which is how the walk follows them.
            if (depth + 1 < maxWalkDepth && entry.is_directory(entryError)
                && !entryError)
                pending.emplace_back(entryPath, depth + 1);
        }
    }

    auto result = Vector<std::string>();
    result.reserve(static_cast<int>(found.size()));

    for (const auto& path: found)
        result.add(path);

    return result;
}

} // namespace MakeASound::Hosting
