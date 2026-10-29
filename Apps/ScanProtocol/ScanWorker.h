#pragma once

#include <MakeASound/Hosting/FormatManager.h>
#include <MakeASound/Hosting/Scan/WorkerCommand.h>

#include <optional>
#include <string>
#include <string_view>

namespace MakeASound::ScanProtocol
{

using Hosting::FormatManager;
using Hosting::ModuleScan;
using Hosting::WorkerCommand;

// The scan worker protocol MakeASound's example apps speak, assembled from the
// hosting library: Apps/PluginScanner answers it, and Apps/PluginHost both
// answers it (relaunched as its own worker) and drives a ScanSession with it. A
// product with its own scanner writes its own instead; nothing in the library
// depends on this one. See Docs/PluginScanning.md.
//
// The transport is argv in, one line of stdout and an exit code out. There is no
// channel to keep alive: a plugin that crashes the worker just means no line.

// The argument that asks for a scan, followed by the target.
inline constexpr auto scanArgument = std::string_view("--makeasound-scan");

// The marker that starts the result line. Plugins print (banners, licence
// chatter, their own logging), so the result is ONE line, starting with this,
// holding the ModuleScan as compact Miro JSON. The reader takes the LAST such
// line and ignores everything around it.
inline constexpr auto scanResultPrefix = std::string_view("MAKEASOUND_SCAN:");

// Exit codes. The host reads anything non-zero as "this target killed the
// worker", so a code is spent only on a request the worker could not act on. A
// plugin that fails to LOAD is an answer (a ModuleScan with `error`) and exits 0.
enum class ScanWorkerExit
{
    ok = 0, // a ModuleScan was printed (loaded, or cleanly failed)
    missingTarget = 2, // the scan argument with nothing after it
    noSuchTarget = 3, // nothing is installed under that name
    unknownTarget = 4 // no format in this build claims the target
};

// `executable` as this protocol's worker: [executable, --makeasound-scan], read
// with decodeScanResult. For a host that ships a separate scanner binary.
WorkerCommand workerCommandFor(std::string executable);

// This executable relaunched as its own worker.
WorkerCommand defaultWorkerCommand();

// The result line for `scan`, without its newline.
std::string encodeScanResult(const ModuleScan& scan);

// Writes that line to stdout the way a worker must: on a line of its own and
// flushed, since the plugin may have left half a line in stdout's buffer.
void printScanResult(const ModuleScan& scan);

// The ModuleScan in a worker's captured stdout, ignoring any noise around it.
// Nullopt when there is no result line, or its JSON does not decode into a scan
// with a path (Miro's reader is forgiving, so the path is the shape check).
std::optional<ModuleScan> decodeScanResult(std::string_view workerOutput);

// Runs the scan when the command line asks for one, and answers the exit code
// for main() to return; nullopt when it does not, so main() carries on. It
// reads the command line eacp stored (eacp::Apps::runConsole or
// setCommandLineArgs), so nothing passes argc/argv down to it:
//
//     int runRequestedMode()
//     {
//         auto formats = FormatManager();
//         formats.addDefaultFormats();
//
//         if (auto code = runScanWorkerIfRequested(formats))
//             return *code;
//         ...
//     }
//
//     int main(int argc, char** argv)
//     {
//         return eacp::Apps::runConsole(argc, argv, runRequestedMode);
//     }
//
// The target is classified and scanned through `formats`
// (FormatManager::scanTarget), so a worker answers about exactly the formats its
// host adds. Call with an event loop available on this thread: loading a plugin
// expects one.
std::optional<int> runScanWorkerIfRequested(const FormatManager& formats);

// The same over an explicit argv, argv[0] skipped, for a main() that has not
// stored its command line with eacp.
std::optional<int>
    runScanWorkerIfRequested(int argc, char** argv, const FormatManager& formats);

// The target after the scan argument in the stored command line, nullopt when
// the argument is absent, and an empty string when it is there with nothing
// after it. Cheap, and loads nothing: a main() asks it to decide between the
// worker and the app before starting either.
std::optional<std::string> requestedScanTarget();

// The same over an explicit argv, argv[0] skipped.
std::optional<std::string> requestedScanTarget(int argc, char** argv);

} // namespace MakeASound::ScanProtocol
