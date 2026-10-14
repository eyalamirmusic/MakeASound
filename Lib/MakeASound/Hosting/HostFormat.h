#pragma once

#include "HostedPlugin.h"
#include "PluginDescription.h"

#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

// One plugin format, as a host sees it: where its plugins live, which ones are
// there, how a target is recognised and stamped, and what is inside one.
// FormatManager owns one per format, and everything format-neutral (the cache,
// the sweep, the scan worker) asks through it, so no format SDK reaches that code.
//
// A TARGET is the string a plugin is addressed by: the module path for a format
// whose plugins are files (VST3), the registry identifier for one whose plugins
// are not (an Audio Unit's "type:subtype:manufacturer"). It is what the cache
// keys on, what a worker process is handed, and ModuleScan::path.
class HostFormat
{
public:
    virtual ~HostFormat() = default;

    virtual PluginFormat format() const = 0;

    // The folders this format installs into on this platform, those that exist.
    // A sweep walks them and records them as where it looked. Empty for a format
    // with no folders (an Audio Unit lives in a registry).
    virtual Vector<std::string> defaultSearchFolders() const = 0;

    // Every target of this format under `folders`, deduplicated and sorted. A
    // registry format ignores the folders.
    virtual Vector<std::string>
        findTargets(const Vector<std::string>& folders) const = 0;

    // Whether `target` is spelled like one of this format's, from its shape alone:
    // nothing is loaded to find out. FormatManager asks the formats in the order
    // they were added and the first to claim a target gets it.
    virtual bool ownsTarget(std::string_view target) const = 0;

    // Whether `target` is installed, which tells a bad request ("there is no such
    // plugin") from a plugin that is there and misbehaved. Defaults to "a file or
    // folder exists at that path".
    virtual bool targetExists(std::string_view target) const;

    // The identity stamp a cached entry is compared against. Defaults to the
    // file stamp (moduleStamp); an Audio Unit stamps as its component version.
    virtual ModuleStamp stamp(std::string_view target) const;

    // Loads the target and describes each class it exposes. This RUNS PLUGIN
    // CODE: it can hang or crash the calling process, which is why a sweep runs
    // it in a worker process. A plugin that refuses to load is an answer, with
    // ModuleScan::error set. Call on the main thread with an event loop
    // available: VST3 module initialisation expects one, and an AUv3 answers
    // through it.
    virtual ModuleScan scan(std::string_view target) const = 0;

    // Loads the class `description` names (its path and class id, never a
    // position, so a selection survives a plugin update) and instantiates it.
    // Runs plugin code in THIS process. Message thread, with an event loop
    // running: module initialisation and an AUv3's asynchronous instantiation
    // need one. The default loads nothing and says so.
    virtual LoadResult load(const PluginDescription& description,
                            const HostInfo& host) const;
};

} // namespace MakeASound::Hosting
