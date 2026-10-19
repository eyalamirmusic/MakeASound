// PluginScanner: a console front end to MakeASound's plugin scanning, and the
// worker it relaunches itself as. A host can ship this binary as its scanner and
// point its own ScanSession at it (Hosting::workerCommandFor), or run it and read
// the database it writes.
//
//   PluginScanner [options]                    sweep (the orchestrator)
//   PluginScanner --makeasound-scan <target>   one target (a worker)
//
// Both modes run on the main thread of a live eacp event loop: a worker needs one
// to load a plugin (VST3 module init, an AUv3's asynchronous instantiation), and
// the sweep runs there too so both share one exit path.

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/Args.h>
#include <eacp/Core/Utils/Strings.h>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace MakeASound;
using namespace MakeASound::Hosting;

namespace
{
namespace Strings = eacp::Strings;

// What a sweep returns besides Args::badArguments. Worker mode returns
// ScanWorkerExit's codes, which share 0 and 2 with these.
constexpr auto exitOk = 0;
constexpr auto exitDatabaseNotWritten = 5;

constexpr auto description =
    R"(Describes the audio plugins installed on this machine.

A sweep finds every VST3 module in the platform's VST3 folders and, on macOS,
every hostable Audio Unit, then loads each one in a WORKER PROCESS: this binary
relaunched with --makeasound-scan, several at a time. A plugin that crashes or
hangs takes down only its own worker and is recorded as crashed.

A sweep is incremental: only plugins that are new or changed since the last one
are loaded. A plugin that failed stays blacklisted until it changes, and one no
longer installed is dropped. --force starts from nothing.)";

constexpr auto epilogue = R"(Worker mode:
  PluginScanner --makeasound-scan <target>

loads one target, a .vst3 module or an Audio Unit identifier
(type:subtype:manufacturer), and prints one line to stdout: MAKEASOUND_SCAN:
followed by the result as compact JSON. See MakeASound's Docs/PluginScanning.md.

Exit codes:
  0  answered (a plugin that failed to load is an answer, not a failure)
  2  the arguments made no sense
  3  worker: nothing is installed under that name
  4  worker: no format claims the target
  5  the database could not be written)";

const char* statusWord(const ModuleScan& scan)
{
    if (scan.crashed)
        return "crashed";

    return scan.error.empty() ? "ok" : "error";
}

std::string detailText(const ModuleScan& scan)
{
    if (!scan.error.empty())
        return scan.error;

    const auto count = scan.classes.size();
    return Strings::concat(count, count == 1 ? " class" : " classes");
}

// Prints what the sweep reports, all of it on stderr: stdout is the worker
// protocol's, and a sweep's stays empty.
int sweep(const FormatManager& formats, ScanSessionOptions options)
{
    auto session = ScanSession(formats, std::move(options));
    auto saved = false;

    session.onStarted = [](const ScanSessionPlan& plan)
    {
        if (plan.couldNotRemoveDatabase)
            std::fprintf(stderr,
                         "PluginScanner: could not remove %s\n",
                         plan.cachePath.c_str());

        std::fprintf(stderr,
                     "PluginScanner: %d VST3 module(s) in %d folder(s), %d Audio "
                     "Unit(s); %d cached, %d to scan; %d worker(s), %d ms timeout\n",
                     plan.targetsOf(PluginFormat::VST3),
                     plan.folders,
                     plan.targetsOf(PluginFormat::AU),
                     plan.cached,
                     plan.toScan,
                     plan.jobs,
                     plan.timeoutMs);
    };

    session.onTargetFinished = [](int index, int total, const ModuleScan& scan)
    {
        std::fprintf(stderr,
                     "[%*d/%d] %-7s %s - %s\n",
                     static_cast<int>(Strings::concat(total).size()),
                     index,
                     total,
                     statusWord(scan),
                     scan.path.c_str(),
                     detailText(scan).c_str());
    };

    session.onFinished = [&saved](const ScanSessionSummary& summary)
    {
        saved = summary.saved;

        std::fprintf(stderr,
                     "PluginScanner: %d scanned in %.1f s - %d ok, %d failed, %d "
                     "crashed\n",
                     summary.scanned,
                     static_cast<double>(summary.elapsedMs) / 1000.0,
                     summary.ok,
                     summary.failed,
                     summary.crashed);

        std::fprintf(stderr,
                     summary.saved ? "PluginScanner: wrote %s\n"
                                   : "PluginScanner: could not write %s\n",
                     summary.cachePath.c_str());
    };

    session.start();

    // A console program drives the sweep itself, with the same poll() an app's
    // timer would call.
    while (session.isRunning())
    {
        session.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return saved ? exitOk : exitDatabaseNotWritten;
}

// Both modes read the command line runConsole stored before calling this.
int runRequestedMode()
{
    auto formats = FormatManager();
    formats.addDefaultFormats();

    // A worker first: its command line is the protocol's, not this tool's.
    if (const auto code = runScanWorkerIfRequested(formats))
        return *code;

    // Its worker is this binary relaunched, the default: runConsole recorded
    // argv[0] before this ran.
    auto options = ScanSessionOptions();

    auto cli = eacp::Args::Parser {"PluginScanner", description};
    addScanOptions(cli, options);
    cli.epilogue(epilogue);

    if (const auto code = cli.exitCodeFor(cli.parse()))
        return *code;

    return sweep(formats, std::move(options));
}
} // namespace

int main(int argc, char** argv)
{
    // The embedded Info.plist says LSUIElement, which keeps this process and
    // every worker off the Dock from the first frame; runConsole hides the icon
    // as well, for a build that lost the section.
    return eacp::Apps::runConsole(argc, argv, runRequestedMode);
}
