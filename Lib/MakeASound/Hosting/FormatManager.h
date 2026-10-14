#pragma once

#include "HostFormat.h"
#include "PluginCache.h"

#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

// Why a target could not be scanned at all, as opposed to a plugin that answered
// with a failure, which is a perfectly good scan.
enum class TargetRequest
{
    ok,
    noSuchTarget, // nothing is installed under that name
    unknownKind // no format in this manager claims the target
};

// What asking about one target produced: a scan (whatever it says, including a
// plugin that refused to load), or the reason the request could not be acted on.
struct TargetScan
{
    ModuleScan scan;
    TargetRequest request = TargetRequest::ok;
    std::string message; // set when request != TargetRequest::ok
};

// The formats a host knows, owned in one place. It replaces process-wide format
// registries: whoever runs a sweep, reads a cache or answers as a scan worker
// passes the manager, so what a binary can host is decided by what it adds here
// rather than by which object files the linker kept.
//
//     auto formats = FormatManager();
//     formats.addDefaultFormats();   // VST3, plus Audio Units on macOS
//
// Message thread. The manager must outlive whatever it is handed to.
class FormatManager
{
public:
    FormatManager();
    ~FormatManager();

    FormatManager(const FormatManager&) = delete;
    FormatManager& operator=(const FormatManager&) = delete;

    // Adds a format. One per PluginFormat: a second one for the same format
    // replaces the first, keeping the first one's place in the claiming order.
    void add(OwningPointer<HostFormat> formatToAdd);

    // Every format this platform hosts, in claiming order. Defined per platform
    // (DefaultFormats-<os>.cpp).
    void addDefaultFormats();

    int size() const noexcept;
    const HostFormat& operator[](int index) const noexcept;

    // The format registered for `format`, or null.
    const HostFormat* find(PluginFormat format) const noexcept;

    // The first format, in the order they were added, that claims `target`, or
    // null when none does.
    const HostFormat* findForTarget(std::string_view target) const;

    // The current stamp of a cached entry, by its format's rule; a format this
    // manager does not hold stamps as a file. This is what a cache shared by
    // several formats must compare against: an Audio Unit has no file, and
    // stamping it as one reads it as stale forever.
    ModuleStamp stamp(const ModuleScan& entry) const;

    // stamp() as a PluginCache::StampFunction. The manager must outlive the cache.
    PluginCache::StampFunction stampFunction() const;

    // The stamp of a target by the format that claims it, for a result that did
    // not come from a scan (every crash is one).
    ModuleStamp stampTarget(std::string_view target) const;

    // The format that claims `target`, or VST3 (ModuleScan's default) when none
    // does.
    PluginFormat formatOfTarget(std::string_view target) const;

    // Scans one target in THIS process, on this thread: classified, checked for
    // existence, then scanned by its format. Runs plugin code; see
    // HostFormat::scan. The result's path and format are the target and the
    // claiming format, whatever the scan echoed.
    TargetScan scanTarget(std::string_view target) const;

    // Loads a scanned plugin through the format it names: a null plugin and an
    // error naming the format when this manager holds none for it (an Audio
    // Unit row read on Windows out of a database shared with a Mac, say).
    // Message thread; see HostFormat::load.
    LoadResult load(const PluginDescription& description,
                    const HostInfo& host = {}) const;

private:
    Vector<OwningPointer<HostFormat>> formats;
};

// A cache whose entries stamp by their format, through `formats`, which must
// outlive it. What a host that only READS a shared database wants.
inline void useFormatStamps(PluginCache& cache, const FormatManager& formats)
{
    cache.setStampFunction(formats.stampFunction());
}

} // namespace MakeASound::Hosting
