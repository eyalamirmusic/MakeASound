#pragma once

#include "../HostFormat.h"

#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

// CLAP, as a host sees it. A target is a .clap module: a bundle on macOS, the
// shared library itself on Windows and Linux. One module can hold several
// plugins, each described by its factory's descriptor, with the descriptor's id
// (a reverse-DNS string such as "com.vendor.plugin") as its class id. The CLAP
// headers stay in the implementation.
//
// Scanning only, for now: load() is HostFormat's, which loads nothing and says
// so.
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
    // deinit()ed once the scan is done.
    ModuleScan scan(std::string_view target) const override;
};

} // namespace MakeASound::Hosting
