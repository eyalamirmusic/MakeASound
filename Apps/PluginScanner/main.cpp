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

#include "Options.h"

#include <eacp/Core/App/App.h>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace MakeASound;
using namespace MakeASound::Hosting;

namespace
{
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

    return std::to_string(scan.classes.size())
           + (scan.classes.size() == 1 ? " class" : " classes");
}

ScanSessionOptions sessionOptionsFor(const PluginScanner::Options& options)
{
    auto result = ScanSessionOptions();

    result.cachePath = options.outputPath;
    result.searchFolders = options.folders;
    result.useDefaultFolders = options.useDefaultFolders;
    result.formats = options.formats;
    result.forceRescan = options.force;
    result.jobs = options.jobs;
    result.timeoutMs = options.timeoutMs;

    // The workers are this binary relaunched, the default.
    result.worker = defaultWorkerCommand();

    return result;
}

// Prints what the sweep reports, all of it on stderr: stdout is the worker
// protocol's, and a sweep's stays empty.
int sweep(const FormatManager& formats, const PluginScanner::Options& options)
{
    auto session = ScanSession(formats, sessionOptionsFor(options));
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
                     static_cast<int>(std::to_string(total).size()),
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

    return static_cast<int>(saved ? PluginScanner::Exit::ok
                                  : PluginScanner::Exit::databaseNotWritten);
}

int runRequestedMode(int argc, char** argv)
{
    auto formats = FormatManager();
    formats.addDefaultFormats();

    // A worker first: its command line is the protocol's, not this tool's.
    if (const auto code = runScanWorkerIfRequested(argc, argv, formats))
        return *code;

    const auto options = PluginScanner::parseOptions(argc, argv);

    if (!options.error.empty())
    {
        std::fprintf(stderr, "PluginScanner: %s\n", options.error.c_str());
        return static_cast<int>(PluginScanner::Exit::badArguments);
    }

    if (options.mode == PluginScanner::Mode::help)
    {
        std::fputs(PluginScanner::helpText().c_str(), stdout);
        return static_cast<int>(PluginScanner::Exit::ok);
    }

    return sweep(formats, options);
}
} // namespace

int main(int argc, char** argv)
{
    // Before the loop, so the worker command relaunches argv[0].
    eacp::Apps::setCommandLineArgs(argc, argv);

    return eacp::Apps::run(
        [argc, argv]
        {
            // The embedded Info.plist already says LSUIElement, which keeps this
            // process and every worker off the Dock from the first frame; this
            // confirms it for a build that lost the section. Inside the callback,
            // because the loop sets the policy on its way in.
            eacp::Apps::setDockIconVisible(false);
            eacp::Apps::quit(runRequestedMode(argc, argv));
        });
}
