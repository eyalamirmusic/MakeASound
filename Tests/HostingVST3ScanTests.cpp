// Scanning real VST3 modules: the fixtures this tree builds (Tests/HostFixtures),
// in process for the well-behaved ones and through a worker for the one that
// crashes and the one that hangs, and a whole sweep of them into the cache.

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifndef MAKEASOUND_HOST_FIXTURE_VST3_DIR
#error "MAKEASOUND_HOST_FIXTURE_VST3_DIR must name the fixtures' VST3 folder"
#endif

namespace HostingVST3ScanTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;

namespace
{
namespace fs = std::filesystem;

std::string fixture(const char* name)
{
    const auto folder = fs::path(MAKEASOUND_HOST_FIXTURE_VST3_DIR);

    return (folder / (std::string(name) + ".vst3")).string();
}

fs::path freshFolder(const char* name)
{
    auto folder = fs::temp_directory_path() / "MakeASoundHostingVST3Tests" / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

struct DefaultFormats : FormatManager
{
    DefaultFormats() { addDefaultFormats(); }
};

WorkerCommand realWorker()
{
    auto command = WorkerCommand();
    command.executable = MAKEASOUND_FAKE_SCAN_WORKER;
    command.argumentPrefix = {"--fake-worker", "formats", std::string(scanArgument)};
    return command;
}

template <typename Runner>
bool pumpUntilFinished(Runner& runner, int budgetMs = 60000)
{
    const auto start = std::chrono::steady_clock::now();

    while (runner.isRunning()
           && std::chrono::steady_clock::now() - start
                  < std::chrono::milliseconds(budgetMs))
    {
        runner.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    return !runner.isRunning();
}

ModuleScan scanThroughWorker(const FormatManager& formats,
                             const std::string& target,
                             int timeoutMs = 30000)
{
    auto coordinator = ScanCoordinator(formats);
    auto scans = Vector<ModuleScan>();

    coordinator.onModuleScanned = [&scans](const ModuleScan& scan)
    { scans.add(scan); };

    coordinator.setWorkerCommand(realWorker());
    coordinator.setTimeoutMs(timeoutMs);
    coordinator.start({target});

    check(pumpUntilFinished(coordinator));
    check(scans.size() == 1);

    return scans.size() == 1 ? scans[0] : ModuleScan();
}

auto tFormat = test("HostingVST3Scan/theFormatClaimsVST3Modules") = []
{
    const auto formats = DefaultFormats();
    const auto* vst3 = formats.find(PluginFormat::VST3);

    check(vst3 != nullptr);
    check(formats.findForTarget("/a/Plugin.vst3") == vst3);
    check(formats.findForTarget("C:/a/Plugin.VST3") == vst3);
    check(formats.findForTarget("/a/Plugin.component") != vst3);

    for (const auto& folder: vst3->defaultSearchFolders())
        check(fs::is_directory(folder));

    const auto targets = vst3->findTargets({MAKEASOUND_HOST_FIXTURE_VST3_DIR});
    check(targets.size() == 4);
    check(targets.contains(fixture("HostFixtureEffect")));
    check(targets.contains(fixture("HostFixtureScanCrash")));
};

auto tEffect = test("HostingVST3Scan/anEffectIsDescribed") = []
{
    const auto formats = DefaultFormats();
    const auto outcome = formats.scanTarget(fixture("HostFixtureEffect"));

    check(outcome.request == TargetRequest::ok);

    const auto& scan = outcome.scan;
    check(scan.succeeded());
    check(scan.format == PluginFormat::VST3);
    check(scan.stamp() == moduleStamp(fixture("HostFixtureEffect")));
    check(scan.stamp().fileSize > 0);
    check(scan.classes.size() == 1);

    const auto& effect = scan.classes[0];
    check(effect.format == PluginFormat::VST3);
    check(effect.path == scan.path);
    check(effect.name == "HostFixtureEffect");
    check(effect.vendor == "MakeASound Tests");
    check(effect.version == "1.0.0");
    check(effect.category == "Audio Module Class");
    check(effect.classId.size() == 32);
    check(!effect.isInstrument());
    check(effect.numAudioInputs == 3);
    check(effect.numAudioOutputs == 2);
    check(!effect.hasMidiInput);
};

auto tInstrument = test("HostingVST3Scan/anInstrumentIsDescribed") = []
{
    const auto formats = DefaultFormats();
    const auto scan = formats.scanTarget(fixture("HostFixtureInstrument")).scan;

    check(scan.succeeded());
    check(scan.classes.size() == 1);

    const auto& instrument = scan.classes[0];
    check(instrument.name == "HostFixtureInstrument");
    check(instrument.isInstrument());
    check(instrument.numAudioInputs == 0);
    check(instrument.numAudioOutputs == 2);
    check(instrument.hasMidiInput);
};

auto tBroken = test("HostingVST3Scan/aModuleThatCannotLoadIsACleanError") = []
{
    const auto folder = freshFolder("broken");
    const auto broken = folder / "Broken.vst3";
    fs::create_directories(broken / "Contents" / "MacOS");
    fs::create_directories(broken / "Contents" / "x86_64-win");

    {
        auto out = std::ofstream(broken / "Contents" / "MacOS" / "Broken");
        out << "junk";
    }

    const auto formats = DefaultFormats();
    const auto outcome = formats.scanTarget(broken.string());

    check(outcome.request == TargetRequest::ok);
    check(!outcome.scan.crashed);
    check(!outcome.scan.error.empty());
    check(outcome.scan.classes.empty());

    fs::remove_all(folder);
};

auto tWorker = test("HostingVST3Scan/aWorkerDescribesWhatItLoads") = []
{
    const auto formats = DefaultFormats();
    const auto scan = scanThroughWorker(formats, fixture("HostFixtureEffect"));

    check(scan.succeeded());
    check(scan.classes.size() == 1);
    check(scan.classes[0].name == "HostFixtureEffect");
    check(scan.classes[0].numAudioInputs == 3);
};

auto tCrash = test("HostingVST3Scan/aPluginThatCrashesIsRecordedAsACrash") = []
{
    const auto formats = DefaultFormats();
    const auto target = fixture("HostFixtureScanCrash");
    const auto scan = scanThroughWorker(formats, target);

    check(scan.crashed);
    check(scan.format == PluginFormat::VST3);
    check(scan.stamp() == moduleStamp(target));
    check(scan.error.find("exited with code") != std::string::npos);
};

auto tHang = test("HostingVST3Scan/aPluginThatHangsIsKilledAtTheTimeout") = []
{
    const auto formats = DefaultFormats();
    const auto target = fixture("HostFixtureScanHang");
    const auto scan = scanThroughWorker(formats, target, 2000);

    check(scan.crashed);
    check(scan.error.find("killed after 2000 ms") != std::string::npos);
    check(scan.stamp() == moduleStamp(target));
};

auto tSweep = test("HostingVST3Scan/aSweepOfTheFixturesBlacklistsTheBadOnes") = []
{
    const auto folder = freshFolder("sweep");
    const auto formats = DefaultFormats();

    auto options = ScanSessionOptions();
    options.cachePath = (folder / "Plugins.json").string();
    options.searchFolders = {MAKEASOUND_HOST_FIXTURE_VST3_DIR};
    options.useDefaultFolders = false;
    options.formats = {PluginFormat::VST3};
    options.worker = realWorker();
    options.timeoutMs = 2000;

    auto run = [&]
    {
        auto session = ScanSession(formats, options);
        auto summary = ScanSessionSummary();
        auto plan = ScanSessionPlan();

        session.onStarted = [&plan](const ScanSessionPlan& p) { plan = p; };
        session.onFinished = [&summary](const ScanSessionSummary& s)
        { summary = s; };
        session.start();

        check(pumpUntilFinished(session));
        return std::make_pair(plan, summary);
    };

    const auto [plan, summary] = run();
    check(plan.found == 4);
    check(plan.toScan == 4);
    check(summary.ok == 2);
    check(summary.crashed == 2);
    check(summary.saved);

    auto cache = PluginCache(options.cachePath);
    useFormatStamps(cache, formats);
    check(cache.load());
    check(cache.plugins().size() == 2);
    check(cache.isBlacklisted(fixture("HostFixtureScanCrash")));
    check(cache.isBlacklisted(fixture("HostFixtureScanHang")));
    check(!cache.isBlacklisted(fixture("HostFixtureEffect")));

    // Nothing changed, so nothing is loaded again, crashes included.
    const auto [again, againSummary] = run();
    check(again.found == 4);
    check(again.toScan == 0);
    check(againSummary.scanned == 0);

    fs::remove_all(folder);
};
} // namespace
} // namespace HostingVST3ScanTests
