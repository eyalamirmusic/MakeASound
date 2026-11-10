// Apps/PluginScanner run as a program: its arguments and exit codes, its worker
// mode answering the protocol, and a sweep of the fixture plugins into a database
// with a silent stdout.

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <ScanProtocol/ScanWorker.h>

#include "HostFixtureCopies.h"

#include <eacp/Core/Process/Process.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <string>

#ifndef MAKEASOUND_PLUGIN_SCANNER
#error "MAKEASOUND_PLUGIN_SCANNER must name the PluginScanner executable"
#endif

namespace HostingScannerAppTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;
using namespace MakeASound::ScanProtocol;

namespace
{
namespace fs = std::filesystem;

eacp::Processes::ProcessResult scanner(const Vector<std::string>& arguments)
{
    auto options = eacp::Processes::ProcessOptions();
    options.executable = MAKEASOUND_PLUGIN_SCANNER;
    options.arguments = arguments;
    options.noWindow = true;

    return eacp::Processes::run(std::move(options));
}

std::string fixture(const char* name)
{
    const auto folder = fs::path(MAKEASOUND_HOST_FIXTURE_VST3_DIR);

    return (folder / (std::string(name) + ".vst3")).string();
}

auto tHelp = test("HostingScannerApp/helpDescribesBothModes") = []
{
    const auto result = scanner({"--help"});

    check(result.exitCode == 0);
    check(result.output.find("--makeasound-scan") != std::string::npos);
    check(result.output.find("--output") != std::string::npos);
};

auto tBadArguments = test("HostingScannerApp/badArgumentsExitWithTwo") = []
{
    check(scanner({"--bogus"}).exitCode == 2);
    check(scanner({"--jobs", "0"}).exitCode == 2);
    check(scanner({"--format", "clap"}).exitCode == 2);
    check(scanner({"--output"}).exitCode == 2);
    check(scanner({"--makeasound-scan"}).exitCode == 2);
};

auto tWorker = test("HostingScannerApp/workerModeAnswersOneLine") = []
{
    const auto result =
        scanner({"--makeasound-scan", fixture("HostFixtureInstrument")});

    check(result.exitCode == 0);

    const auto scan = decodeScanResult(result.output);
    check(scan.has_value());
    check(scan->succeeded());
    check(scan->classes.size() == 1);
    check(scan->classes[0].name == "HostFixtureInstrument");
    check(scan->classes[0].isInstrument());
};

auto tWorkerRequests = test("HostingScannerApp/workerModeRefusesBadRequests") = []
{
    const auto missing = scanner({"--makeasound-scan", fixture("Missing")});
    check(missing.exitCode == 3);
    check(!decodeScanResult(missing.output).has_value());

    check(scanner({"--makeasound-scan", "/a/readme.txt"}).exitCode == 4);
};

auto tSweep = test("HostingScannerApp/aSweepWritesTheDatabaseAndStaysQuiet") = []
{
    const auto folder =
        fs::temp_directory_path() / "MakeASoundHostingScannerAppTests" / "sweep";
    fs::remove_all(folder);

    const auto database = (folder / "Plugins.json").string();
    const auto arguments = Vector<std::string> {"--output",
                                                database,
                                                "--folder",
                                                MAKEASOUND_HOST_FIXTURE_VST3_DIR,
                                                "--no-default-folders",
                                                "--format",
                                                "vst3",
                                                "--timeout",
                                                "2000"};

    const auto first = scanner(arguments);
    check(first.exitCode == 0);
    check(first.output.empty());
    check(first.errorOutput.find("4 to scan") != std::string::npos);
    check(first.errorOutput.find("2 ok, 0 failed, 2 crashed") != std::string::npos);

    auto cache = PluginCache(database);
    check(cache.load());
    check(cache.plugins().size() == 2);
    check(cache.isBlacklisted(fixture("HostFixtureScanCrash")));
    check(cache.isBlacklisted(fixture("HostFixtureScanHang")));

    const auto second = scanner(arguments);
    check(second.exitCode == 0);
    check(second.errorOutput.find("4 cached, 0 to scan") != std::string::npos);

    auto forced = arguments;
    forced.add("--force");
    check(scanner(forced).errorOutput.find("0 cached, 4 to scan")
          != std::string::npos);

    fs::remove_all(folder);
};

// The worker's target, and the sweep's folder, reach the scanner as program
// arguments: they arrive as the UTF-8 the host sent, not in the system's code
// page, and the module then loads from that path.
auto tWorkerNonAscii = test("HostingScannerApp/workerModeScansANonAsciiPath") = []
{
    const auto bundle = HostFixtureCopies::copyIntoNonAsciiFolder(
        MAKEASOUND_HOST_FIXTURE_VST3_DIR,
        "HostFixtureEffect",
        fs::temp_directory_path() / "MakeASoundHostingScannerAppTests" / "worker");

    const auto result = scanner({"--makeasound-scan", bundle});
    check(result.exitCode == 0);

    const auto scan = decodeScanResult(result.output);
    check(scan.has_value());

    if (!scan)
        return;

    check(scan->error.empty());
    check(scan->succeeded());
    check(scan->path == bundle);
    check(scan->classes.size() == 1);
};

auto tSweepNonAscii =
    test("HostingScannerApp/aSweepOfANonAsciiFolderScansAndLoads") = []
{
    const auto root =
        fs::temp_directory_path() / "MakeASoundHostingScannerAppTests" / "sweepUtf8";
    const auto bundle = HostFixtureCopies::copyIntoNonAsciiFolder(
        MAKEASOUND_HOST_FIXTURE_VST3_DIR, "HostFixtureEffect", root);
    const auto folder =
        HostFixtureCopies::utf8Text(root / HostFixtureCopies::nonAsciiFolderName());
    const auto database = HostFixtureCopies::utf8Text(root / "Plugins.json");

    const auto result = scanner({"--output",
                                 database,
                                 "--folder",
                                 folder,
                                 "--no-default-folders",
                                 "--format",
                                 "vst3",
                                 "--timeout",
                                 "20000"});
    check(result.exitCode == 0);
    check(result.errorOutput.find("1 to scan") != std::string::npos);
    check(result.errorOutput.find("1 ok, 0 failed, 0 crashed") != std::string::npos);

    auto cache = PluginCache(database);
    check(cache.load());
    check(cache.plugins().size() == 1);

    if (cache.plugins().size() != 1)
        return;

    auto formats = FormatManager();
    formats.addDefaultFormats();

    auto loaded = formats.load(cache.plugins()[0], {.name = "Hosting Tests"});
    check(loaded.error.empty());
    check(loaded.plugin != nullptr);

    if (loaded.plugin != nullptr)
        check(loaded.plugin->name() == "HostFixtureEffect");
};
} // namespace
} // namespace HostingScannerAppTests
