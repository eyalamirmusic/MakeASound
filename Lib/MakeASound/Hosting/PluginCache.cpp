#include "PluginCache.h"
#include "Scan/Scanner.h"

#include <Miro/Reflection/Serialize.h>
#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>

namespace MakeASound::Hosting
{

namespace
{
namespace fs = std::filesystem;

constexpr auto cacheFileName = "Plugins.json";

std::string cachePathIn(eacp::FilePath folder)
{
    // No application-data root: keep persistence working in the temp folder
    // rather than failing outright.
    if (folder.empty())
        folder = eacp::FilePath(fs::temp_directory_path().string());

    return (folder / cacheFileName).str();
}

std::set<std::string> asSet(const Vector<std::string>& paths)
{
    auto result = std::set<std::string>();

    for (const auto& path: paths)
        result.insert(path);

    return result;
}
} // namespace

std::string defaultPluginCachePath(std::string_view appName, std::string_view company)
{
    if (eacp::FilePath::appDataDirectory().empty())
        return cachePathIn({});

    return cachePathIn(eacp::FilePath::appSupportDirectory(company, appName));
}

std::string defaultPluginCachePath()
{
    if (eacp::FilePath::appDataDirectory().empty())
        return cachePathIn({});

    return cachePathIn(eacp::FilePath::appSupportDirectory());
}

PluginCache::PluginCache(std::string filePathToUse)
    : filePath(std::move(filePathToUse))
{
}

void PluginCache::changed() const
{
    if (onChanged)
        onChanged();
}

void PluginCache::setStampFunction(StampFunction stampFunctionToUse)
{
    stampFunction = std::move(stampFunctionToUse);
}

bool PluginCache::stampMatches(const ModuleScan& entry) const
{
    const auto stamp = stampFunction ? stampFunction(entry) : moduleStamp(entry.path);

    return stamp == entry.stamp();
}

bool PluginCache::load()
{
    doc = PluginCacheDocument();

    const auto content = eacp::Files::readFile(eacp::FilePath(filePath));

    if (content.empty())
        return false;

    try
    {
        // Json::parse first: createFromJSONString swallows malformed input and
        // answers an all-defaults object, so a truncated file would read as a
        // valid empty cache and overwrite itself on the next save.
        doc = Miro::createFromJSON<PluginCacheDocument>(Miro::Json::parse(content));
    }
    catch (...)
    {
        doc = PluginCacheDocument();
        return false;
    }

    changed();
    return true;
}

bool PluginCache::save() const
{
    const auto json = Miro::toJSONString(doc, 2);

    try
    {
        auto ec = std::error_code();
        fs::create_directories(fs::path(filePath).parent_path(), ec);

        // Atomic: a reader, or a crash, sees the whole old file or the whole new
        // one, never a half-written file that would read as corrupt.
        eacp::Files::writeFileAtomically(
            eacp::FilePath(filePath),
            {reinterpret_cast<const std::uint8_t*>(json.data()), json.size()});
    }
    catch (...)
    {
        return false;
    }

    return true;
}

bool PluginCache::clear()
{
    auto ec = std::error_code();

    // remove() answers false for a file that was not there, which is fine; only
    // a file that survives the call is a failure.
    fs::remove(fs::path(filePath), ec);

    const auto hadContent = !doc.modules.empty() || !doc.searchPaths.empty();

    // Emptied whether or not the file went: a caller who asked for a clean slate
    // must not keep the old answers because a stale file could not be unlinked.
    doc = PluginCacheDocument();

    if (hadContent)
        changed();

    return !ec;
}

void PluginCache::setSearchPaths(Vector<std::string> pathsToUse)
{
    if (doc.searchPaths == pathsToUse)
        return;

    doc.searchPaths = std::move(pathsToUse);
    changed();
}

const ModuleScan* PluginCache::find(std::string_view path) const
{
    for (const auto& module: doc.modules)
        if (module.path == path)
            return &module;

    return nullptr;
}

Vector<PluginDescription> PluginCache::plugins() const
{
    auto result = Vector<PluginDescription>();

    for (const auto& module: doc.modules)
        for (const auto& description: module.classes)
            result.add(description);

    std::sort(result.begin(),
              result.end(),
              [](const PluginDescription& a, const PluginDescription& b)
              {
                  if (a.name != b.name)
                      return a.name < b.name;

                  if (a.path != b.path)
                      return a.path < b.path;

                  return a.classId < b.classId;
              });

    return result;
}

Vector<std::string>
    PluginCache::modulesNeedingScan(const Vector<std::string>& foundTargets) const
{
    auto result = Vector<std::string>();

    for (const auto& path: foundTargets)
    {
        const auto* entry = find(path);

        if (entry == nullptr || !stampMatches(*entry))
            result.add(path);
    }

    return result;
}

void PluginCache::apply(const ModuleScan& scan)
{
    for (auto& module: doc.modules)
    {
        if (module.path != scan.path)
            continue;

        module = scan;
        changed();
        return;
    }

    doc.modules.add(scan);
    changed();
}

void PluginCache::removeMissing(const Vector<std::string>& foundTargets)
{
    const auto present = asSet(foundTargets);
    auto kept = Vector<ModuleScan>();

    for (const auto& module: doc.modules)
        if (present.count(module.path) != 0)
            kept.add(module);

    if (kept.size() == doc.modules.size())
        return;

    doc.modules = std::move(kept);
    changed();
}

void PluginCache::removeMissing(const Vector<std::string>& foundTargets,
                                PluginFormat format)
{
    const auto present = asSet(foundTargets);
    auto kept = Vector<ModuleScan>();

    for (const auto& module: doc.modules)
        if (module.format != format || present.count(module.path) != 0)
            kept.add(module);

    if (kept.size() == doc.modules.size())
        return;

    doc.modules = std::move(kept);
    changed();
}

bool PluginCache::isBlacklisted(std::string_view path) const
{
    const auto* entry = find(path);

    if (entry == nullptr || entry->succeeded())
        return false;

    return stampMatches(*entry);
}

} // namespace MakeASound::Hosting
