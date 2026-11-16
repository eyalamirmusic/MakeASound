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
                "another place to look for VST3 and CLAP modules")
        .hint("<dir>")
        .unique();

    parser
        .flag("--no-default-folders",
              options.useDefaultFolders,
              "look only in the --folder paths")
        .negated();

    // PluginFormat names every format the framework knows; the library hosts
    // these three. One the FormatManager does not hold (AU off macOS, CLAP in a
    // build without it) is a valid choice that sweeps nothing.
    parser
        .option("--format",
                options.formats,
                "sweep only this format. Cached entries of other formats are "
                "left alone")
        .choices({PluginFormat::VST3, PluginFormat::AU, PluginFormat::CLAP})
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
