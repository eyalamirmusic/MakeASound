#pragma once

#include "../HostFormat.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

class CLAPModule;

// CLAP, as a host sees it. A target is a .clap module: a bundle on macOS, the
// shared library itself on Windows and Linux. One module can hold several
// plugins, each described by its factory's descriptor, with the descriptor's id
// (a reverse-DNS string such as "com.vendor.plugin") as its class id. The CLAP
// headers stay in the implementation.
//
// load() makes a CLAPHostedPlugin (CLAPHostedPlugin.h, which has the threading).
//
// A module is loaded, and its entry init()ed, once per path per CLAPFormat:
// every scan and load through this instance shares it while anything made from
// it lives, and the entry is deinit()ed when the last of those lets go. The cache
// is this instance's (no process-wide registry), so two CLAPFormats loading one
// path each init() its entry, which the spec allows (entry.h: a plugin's init
// and deinit must be defensive about being called more than once).
class CLAPFormat : public HostFormat
{
public:
    PluginFormat format() const override;

    // The platform's CLAP folders that exist (CLAPFolders-<os>.cpp), after the
    // entries of $CLAP_PATH (':'-separated, ';' on Windows), as the CLAP spec
    // (entry.h) lists them:
    //
    //   macOS    ~/Library/Audio/Plug-Ins/CLAP, /Library/Audio/Plug-Ins/CLAP
    //   Windows  %COMMONPROGRAMFILES%\CLAP, %LOCALAPPDATA%\Programs\Common\CLAP
    //   Linux    ~/.clap, /usr/lib/clap
    //
    // Resolved in the host: a scan worker is handed a target, never a folder.
    Vector<std::string> defaultSearchFolders() const override;

    Vector<std::string>
        findTargets(const Vector<std::string>& folders) const override;

    // Anything named *.clap, whatever its case.
    bool ownsTarget(std::string_view target) const override;

    // Loads the module, init()s its entry with the target's path, and describes
    // every plugin its factory lists: id, name, vendor, version, a category from
    // its features ("Instrument", "Note Effect" or "Audio Effect") and the
    // features themselves '|'-separated as its sub-categories. Each plugin is
    // created and init()ed (never activated) just long enough to ask its audio
    // ports, note ports and whether it has a gui, then destroyed. The entry is
    // deinit()ed once the scan is done, unless a plugin loaded through this
    // instance still holds the module.
    ModuleScan scan(std::string_view target) const override;

    // Loads the module, finds the plugin whose descriptor id is the class id,
    // and creates and init()s it against a host of its own. The module stays
    // loaded, and its entry init()ed, while the plugin lives; every instance of
    // that module shares it.
    LoadResult load(const PluginDescription& description,
                    const HostInfo& host) const override;

    // The module at `path`, its entry init()ed: the one already loaded through
    // this instance while anything still holds it, else loaded now. Null, with
    // `error` saying why, when it will not load. What scan() and load() use.
    std::shared_ptr<CLAPModule> openModule(std::string_view path,
                                           std::string& error) const;

private:
    mutable std::mutex modulesLock;
    mutable std::map<std::string, std::weak_ptr<CLAPModule>, std::less<>> modules;
};

} // namespace MakeASound::Hosting
