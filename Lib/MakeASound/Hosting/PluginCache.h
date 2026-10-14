#pragma once

#include "PluginDescription.h"

#include <functional>
#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

// The file a cache is saved as: one ModuleScan per target ever seen, plus the
// folders the last sweep looked in. Failures are kept, not dropped; that is what
// makes the blacklist work.
//
// The schema is Tamber Plug's, field for field, so a database Plug wrote loads
// here and one written here loads in Plug. It is append-only: a new field gets a
// default that reads old files correctly, and `version` changes only if a field
// changes meaning.
struct PluginCacheDocument
{
    MIRO_REFLECT(version, searchPaths, modules)

    int version = 1;
    Vector<std::string> searchPaths;
    Vector<ModuleScan> modules;
};

// The default database for an app: Plugins.json in the app's own folder under the
// platform's application-data root (~/Library/Application Support/<company>/<app>
// on macOS, %APPDATA%\<company>\<app> on Windows). The company level is left out
// when empty.
//
// Application data rather than a cache folder: the list is rebuildable but
// user-visible, and a cache folder can be evicted between a scan and the launch
// meant to read it. Falls back to the temp folder when the platform has no
// application-data root (a sandboxed CI runner).
//
// A host is free to put its database anywhere else, including one shared by
// several apps of the same vendor: every API that takes a cache takes a path.
std::string defaultPluginCachePath(std::string_view appName,
                                   std::string_view company = {});

// The same for this process: the app and company names come from eacp's embedded
// AppInfo, and an app without one is named after its executable.
std::string defaultPluginCachePath();

// The scan cache. It owns a PluginCacheDocument, answers "what do I know?" and
// "what still needs scanning?", and persists as JSON.
//
// Persistence is best effort and never throws: a missing or corrupt file reads
// as an empty cache.
class PluginCache
{
public:
    explicit PluginCache(std::string filePathToUse);

    // How an entry's CURRENT identity is read, which is what its stored stamp is
    // compared against. It takes the whole entry, so the answer can depend on the
    // format: an Audio Unit is a registry entry with no file to stat.
    using StampFunction = std::function<ModuleStamp(const ModuleScan&)>;

    const std::string& path() const noexcept { return filePath; }

    // Replaces the document with the file's contents. False, leaving the
    // document empty, when the file is missing, unreadable or malformed.
    bool load();

    // Writes the document atomically, creating parent folders. False on I/O
    // failure.
    bool save() const;

    // Deletes the file and empties the document, search paths included. A forced
    // rescan calls this BEFORE scanning, so an interrupted run leaves no stale
    // file behind. False only when the file is there and could not be removed.
    bool clear();

    const PluginCacheDocument& document() const noexcept { return doc; }

    const Vector<std::string>& searchPaths() const noexcept
    {
        return doc.searchPaths;
    }

    void setSearchPaths(Vector<std::string> pathsToUse);

    // Replaces how an entry's current stamp is read. The default stamps every
    // entry as a file (moduleStamp of its path), which is right for VST3 and
    // wrong for an Audio Unit; a sweep installs its formats' rule here. An empty
    // function restores the default.
    void setStampFunction(StampFunction stampFunctionToUse);

    // Every class of every successful entry, flattened and sorted by name, then
    // path, then class id, so the order is stable.
    Vector<PluginDescription> plugins() const;

    // The entry for `path`, or null when it was never scanned.
    const ModuleScan* find(std::string_view path) const;

    // Which of `foundTargets` must be scanned: those with no entry, and those
    // whose stamp changed. A failed entry with an unchanged stamp is left out;
    // that is the blacklist.
    Vector<std::string>
        modulesNeedingScan(const Vector<std::string>& foundTargets) const;

    // Upserts a result by path. A failure overwrites a good entry as readily as
    // the reverse: it is the current truth about that target.
    void apply(const ModuleScan& scan);

    // Drops the entries not in `foundTargets`, so an uninstalled plugin leaves
    // the list.
    void removeMissing(const Vector<std::string>& foundTargets);

    // The same, restricted to one format: entries of other formats stay whatever
    // `foundTargets` says. A sweep that looked only for VST3 must use this, or it
    // would evict every Audio Unit it never looked for.
    void removeMissing(const Vector<std::string>& foundTargets, PluginFormat format);

    // True when `path` has a failed entry whose stamp still matches, so a rescan
    // would only repeat the failure.
    bool isBlacklisted(std::string_view path) const;

    // Fired whenever the document changes (load, apply, removeMissing, clear,
    // setSearchPaths), and never for a call that changed nothing.
    std::function<void()> onChanged;

private:
    void changed() const;
    bool stampMatches(const ModuleScan& entry) const;

    std::string filePath;
    PluginCacheDocument doc;
    StampFunction stampFunction;
};

} // namespace MakeASound::Hosting
