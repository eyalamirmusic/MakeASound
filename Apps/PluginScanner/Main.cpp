// PluginScanner: a console front end to MakeASound's plugin scanning, and the
// worker it relaunches itself as. It is the reference assembly of a scanner from
// the hosting library: its worker protocol (Apps/ScanProtocol) and command line
// are its own, and a product writes its own the same way. A host can also ship
// this binary as its scanner and point its own ScanSession at it
// (ScanProtocol::workerCommandFor), or run it and read the database it writes.
//
//   PluginScanner [options]                    sweep (the orchestrator)
//   PluginScanner --makeasound-scan <target>   one target (a worker)
//
// Both modes run on the main thread of a live eacp event loop: a worker needs one
// to load a plugin (VST3 module init, an AUv3's asynchronous instantiation), and
// the sweep runs there too so both share one exit path.

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <ScanProtocol/ScanArgs.h>
#include <ScanProtocol/ScanWorker.h>

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/Args.h>
#include <eacp/Core/Utils/Strings.h>

#include <ResEmbed/ResEmbed.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace MakeASound;
using namespace MakeASound::Hosting;
using namespace MakeASound::ScanProtocol;

namespace
{
namespace Strings = eacp::Strings;

// What a sweep returns besides Args::badArguments. Worker mode returns
// ScanWorkerExit's codes, which share 0 and 2 with these.
constexpr auto exitOk = 0;
constexpr auto exitDatabaseNotWritten = 5;

// The --help text, from Resources/ (embedded by res_embed_add). A file's final
// newline is trimmed, since the parser would print it as one more blank line.
std::string helpText(const std::string& name)
{
    return Strings::trim(ResEmbed::get(name, "PluginScannerHelp").toStringView());
}

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

    // Its worker is this binary relaunched: runConsole recorded argv[0] before
    // this ran.
    auto options = ScanSessionOptions();
    options.worker = defaultWorkerCommand();

    auto cli = eacp::Args::Parser {"PluginScanner", helpText("Description.txt")};
    addScanOptions(cli, options);
    cli.epilogue(helpText("Epilogue.txt"));

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
