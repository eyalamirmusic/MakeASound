#include "ScanArgs.h"

#include <eacp/Core/App/Args.h>

namespace MakeASound::ScanProtocol
{

using namespace Hosting;

void addScanOptions(eacp::Args::Parser& parser, ScanSessionOptions& options)
{
    parser.option("--output", options.cachePath, "the database to write")
        .hint("<file>")
        .defaultText("Plugins.json in this app's own application-data folder");

    parser
        .option("--folder",
                options.searchFolders,
                "another place to look for VST3 modules")
        .hint("<dir>")
        .unique();

    parser
        .flag("--no-default-folders",
              options.useDefaultFolders,
              "look only in the --folder paths")
        .negated();

    // PluginFormat names every format the framework knows; this library hosts
    // these two.
    parser
        .option("--format",
                options.formats,
                "sweep only this format. Cached entries of other formats are "
                "left alone")
        .choices({PluginFormat::VST3, PluginFormat::AU})
        .hint("<format>")
        .defaultText("every format")
        .unique();

    parser.flag("--force",
                options.forceRescan,
                "delete the database first and scan everything");

    parser.option("--jobs", options.jobs, "workers at once")
        .atLeast(1)
        .defaultText("the core count, 1..8");

    parser
        .option("--timeout",
                options.timeoutMs,
                "how long one plugin may take before its worker is killed; 0 "
                "disables the watchdog")
        .atLeast(0)
        .hint("<ms>");
}

} // namespace MakeASound::ScanProtocol
