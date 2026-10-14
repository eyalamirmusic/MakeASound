#pragma once

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <string>

namespace PluginScanner
{
using namespace MakeASound;

// What the process returns in sweep mode. Worker mode returns
// Hosting::ScanWorkerExit's codes, which share 0 and 2 with these.
enum class Exit
{
    ok = 0,
    badArguments = 2,
    databaseNotWritten = 5
};

enum class Mode
{
    sweep, // the default: find every plugin and scan what changed
    help
};

struct Options
{
    Mode mode = Mode::sweep;

    // Where the database goes; empty means this app's own folder
    // (Hosting::defaultPluginCachePath()).
    std::string outputPath;

    Vector<std::string> folders;
    bool useDefaultFolders = true;

    // Empty: every format this platform hosts.
    Vector<PluginFormat> formats;

    bool force = false;
    int jobs = 0;
    int timeoutMs = 30000;

    // Set when the command line did not parse; everything else is then
    // meaningless.
    std::string error;
};

Options parseOptions(int argc, char** argv);

std::string helpText();

} // namespace PluginScanner
