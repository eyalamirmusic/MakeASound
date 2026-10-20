// Scanning third-party plugins, around the formats: the FormatManager, the worker
// protocol and its exit codes, the coordinator's pool (crashes, hangs, timeouts,
// cancellation), and whole sweeps into the cache with the blacklist. Formats are
// stubs (HostingTestFormats.h) and misbehaving workers are FakeScanWorker.

#include "HostingTestFormats.h"

#include <Miro/Reflection/Serialize.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/App/AppEnvironment.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifndef MAKEASOUND_FAKE_SCAN_WORKER
#error "MAKEASOUND_FAKE_SCAN_WORKER must name the FakeScanWorker executable"
#endif

namespace HostingScanTests
{
using namespace nano;
using namespace HostingTestFormats;

namespace
{
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

fs::path freshFolder(const char* name)
{
    auto folder = fs::temp_directory_path() / "MakeASoundHostingScanTests" / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

std::string makeBundle(const fs::path& path, std::size_t bytes = 64)
{
    fs::create_directories(path / "Contents" / "MacOS");
    auto out = std::ofstream(path / "Contents" / "MacOS" / "binary",
                             std::ios::binary | std::ios::trunc);
    out << std::string(bytes, 'x');
    return path.string();
}

std::int64_t msSince(Clock::time_point start)
{
    const auto elapsed = Clock::now() - start;

    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

WorkerCommand fakeWorker(const char* mode, const char* delayMs = nullptr)
{
    auto command = WorkerCommand();
    command.executable = MAKEASOUND_FAKE_SCAN_WORKER;
    command.argumentPrefix.add("--fake-worker");
    command.argumentPrefix.add(mode);

    if (delayMs != nullptr)
    {
        command.argumentPrefix.add("--delay");
        command.argumentPrefix.add(delayMs);
    }

    if (std::string(mode) == "worker")
        command.argumentPrefix.add(std::string(scanArgument));

    return command;
}

// Polls on this thread, as an app's timer would, until the run ends or the
// budget runs out: generous for a loaded machine, bounded so a wedged run fails.
template <typename Runner>
bool pumpUntilFinished(Runner& runner, int budgetMs = 30000)
{
    const auto start = Clock::now();

    while (runner.isRunning() && msSince(start) < budgetMs)
    {
        runner.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    return !runner.isRunning();
}

struct Recorder
{
    void listenTo(ScanCoordinator& coordinator)
    {
        coordinator.onModuleScanned = [this](const ModuleScan& scan)
        { scans.add(scan); };

        coordinator.onProgress = [this](int done, int total)
        {
            lastDone = done;
            lastTotal = total;
        };

        coordinator.onFinished = [this] { ++finished; };
    }

    const ModuleScan* find(const std::string& path) const
    {
        for (const auto& scan: scans)
            if (scan.path == path)
                return &scan;

        return nullptr;
    }

    Vector<ModuleScan> scans;
    int lastDone = -1;
    int lastTotal = -1;
    int finished = 0;
};

// One fake target through a coordinator in Subprocess mode.
ModuleScan scanThroughWorker(const FormatManager& formats,
                             const std::string& target,
                             WorkerCommand command,
                             int timeoutMs = 30000)
{
    auto coordinator = ScanCoordinator(formats);
    auto recorder = Recorder();
    recorder.listenTo(coordinator);

    coordinator.setWorkerCommand(std::move(command));
    coordinator.setTimeoutMs(timeoutMs);
    coordinator.start({target});

    check(pumpUntilFinished(coordinator));
    check(recorder.scans.size() == 1);
    check(recorder.finished == 1);

    return recorder.scans.size() == 1 ? recorder.scans[0] : ModuleScan();
}

auto tExecutable = test("HostingScan/theExecutablePathIsThisBinary") = []
{
    const auto path = fs::path(currentExecutablePath());

    check(path.is_absolute());
    check(fs::exists(path));
    check(path.stem() == "MakeASoundTests");
    check(fs::equivalent(fs::path(executablePathFromPlatform()), path));
};

auto tDefaultWorker = test("HostingScan/theDefaultWorkerIsThisBinaryAndFlag") = []
{
    const auto command = defaultWorkerCommand();

    check(fs::equivalent(fs::path(command.executable),
                         fs::path(currentExecutablePath())));
    check(command.argumentPrefix.size() == 1);
    check(command.argumentPrefix[0] == "--makeasound-scan");

    const auto shipped = workerCommandFor("/Apps/Scanner");
    check(shipped.executable == "/Apps/Scanner");
    check(shipped.argumentPrefix[0] == scanArgument);
};

auto tJobs = test("HostingScan/theDefaultJobCountIsClamped") = []
{
    check(defaultScanJobCount() >= 1);
    check(defaultScanJobCount() <= 8);
};

auto tManagerOrder = test("HostingScan/theFirstFormatToClaimATargetGetsIt") = []
{
    auto formats = FormatManager();
    formats.add(makeOwned<StubFileFormat>());
    formats.add(makeOwned<StubRegistryFormat>());

    check(formats.size() == 2);
    check(formats.find(PluginFormat::AU) == &formats[1]);
    check(formats.find(PluginFormat::AAX) == nullptr);
    check(formats.findForTarget("/a/B.vst3") == &formats[0]);
    check(formats.findForTarget("aufx:dely:appl") == &formats[1]);
    check(formats.findForTarget("/a/readme.txt") == nullptr);
    check(formats.findForTarget("") == nullptr);
    check(formats.formatOfTarget("aufx:dely:appl") == PluginFormat::AU);
    check(formats.formatOfTarget("/a/readme.txt") == PluginFormat::VST3);

    // A second format of the same kind replaces the first, in its place.
    formats.add(makeOwned<StubFileFormat>());
    check(formats.size() == 2);
    check(formats[0].format() == PluginFormat::VST3);
};

auto tManagerScan = test("HostingScan/scanTargetTellsABadRequestFromAnAnswer") = []
{
    const auto folder = freshFolder("managerScan");
    const auto module = makeBundle(folder / "Echo.vst3");
    const auto formats = StubFormats();

    const auto unknown = formats.scanTarget("/a/readme.txt");
    check(unknown.request == TargetRequest::unknownKind);
    check(!unknown.message.empty());

    const auto missing = formats.scanTarget((folder / "Missing.vst3").string());
    check(missing.request == TargetRequest::noSuchTarget);

    const auto found = formats.scanTarget(module);
    check(found.request == TargetRequest::ok);
    check(found.scan.path == module);
    check(found.scan.format == PluginFormat::VST3);
    check(found.scan.classes.size() == 1);
    check(found.scan.classes[0].name == "Echo");
    check(found.scan.stamp() == moduleStamp(module));

    fs::remove_all(folder);
};

auto tManagerStamps = test("HostingScan/aCacheStampsEachEntryByItsFormat") = []
{
    auto formats = FormatManager();
    formats.add(makeOwned<StubFileFormat>());

    auto registry = makeOwned<StubRegistryFormat>();
    auto* units = registry.get();
    units->installed["aumu:Synt:Test"] = 7;
    formats.add(std::move(registry));

    auto crashed = ModuleScan();
    crashed.format = PluginFormat::AU;
    crashed.path = "aumu:Synt:Test";
    crashed.crashed = true;
    crashed.setStamp(formats.stampTarget(crashed.path));
    check(crashed.stamp() == ModuleStamp {7, 0});

    auto cache = PluginCache((freshFolder("managerStamps") / "x.json").string());
    useFormatStamps(cache, formats);
    cache.apply(crashed);

    check(cache.isBlacklisted(crashed.path));

    // An update of the component brings it back for another try.
    units->installed["aumu:Synt:Test"] = 8;
    check(!cache.isBlacklisted(crashed.path));
    check(cache.modulesNeedingScan({crashed.path}).size() == 1);
};

auto tEncodeLine = test("HostingScan/aResultIsOneLineBehindTheMarker") = []
{
    auto scan = StubFileFormat().scan("/a/Multi\nLine.vst3");
    const auto line = encodeScanResult(scan);

    check(line.starts_with("MAKEASOUND_SCAN:"));
    check(line.find('\n') == std::string::npos);

    const auto decoded = decodeScanResult(line);
    check(decoded.has_value());
    check(decoded->path == scan.path);
    check(decoded->classes.size() == 1);
    check(decoded->classes[0].name == scan.classes[0].name);
};

auto tDecodeNoise = test("HostingScan/theLastResultLineIsFoundAmongNoise") = []
{
    auto first = ModuleScan();
    first.path = "/a/First.vst3";

    auto last = ModuleScan();
    last.path = "/a/Last.vst3";

    const auto output = "banner\n" + encodeScanResult(first) + "\nlicence chatter "
                        + encodeScanResult(last) + "\r\ngoodbye\n";

    const auto decoded = decodeScanResult(output);
    check(decoded.has_value());
    check(decoded->path == "/a/Last.vst3");
};

auto tDecodeGarbage = test("HostingScan/garbageAndTheOldMarkerDoNotDecode") = []
{
    check(!decodeScanResult("").has_value());
    check(!decodeScanResult("no marker here\n").has_value());
    check(!decodeScanResult("MAKEASOUND_SCAN:{not json\n").has_value());
    check(!decodeScanResult("MAKEASOUND_SCAN:{\"classes\":[]}\n").has_value());

    // The host and its scanner ship together: Tamber Plug's marker is not read.
    auto scan = ModuleScan();
    scan.path = "/a/Old.vst3";
    check(!decodeScanResult("TAMBERPLUG_SCAN:" + Miro::toJSONString(scan) + "\n")
               .has_value());
};

auto tWorkerArguments = test("HostingScan/theWorkerAnswersOnlyWhenAsked") = []
{
    const auto formats = StubFormats();

    char program[] = "host";
    char other[] = "--verbose";
    char flag[] = "--makeasound-scan";
    char unknown[] = "/a/readme.txt";
    char missing[] = "/no/such/Missing.vst3";

    char* none[] = {program, other};
    check(!runScanWorkerIfRequested(2, none, formats).has_value());

    char* empty[] = {program, flag};
    check(runScanWorkerIfRequested(2, empty, formats)
          == static_cast<int>(ScanWorkerExit::missingTarget));

    char* unclaimed[] = {program, flag, unknown};
    check(runScanWorkerIfRequested(3, unclaimed, formats)
          == static_cast<int>(ScanWorkerExit::unknownTarget));

    char* absent[] = {program, flag, missing};
    check(runScanWorkerIfRequested(3, absent, formats)
          == static_cast<int>(ScanWorkerExit::noSuchTarget));
};

auto tWorkerStoredArguments =
    test("HostingScan/theWorkerReadsTheCommandLineEacpStored") = []
{
    const auto formats = StubFormats();
    auto& stored = eacp::Apps::getAppEnvironment().commandLineArgs;
    const auto saved = stored;

    char program[] = "--makeasound-scan";
    char other[] = "--verbose";
    char* none[] = {program, other};
    eacp::Apps::setCommandLineArgs(2, none);
    const auto programIsSkipped = !requestedScanTarget().has_value()
                                  && !runScanWorkerIfRequested(formats).has_value();

    char host[] = "host";
    char flag[] = "--makeasound-scan";
    char unknown[] = "/a/readme.txt";
    char* unclaimed[] = {host, flag, unknown};
    eacp::Apps::setCommandLineArgs(3, unclaimed);
    const auto target = requestedScanTarget();
    const auto code = runScanWorkerIfRequested(formats);

    stored = saved;

    check(programIsSkipped);
    check(target == std::optional<std::string>("/a/readme.txt"));
    check(code == static_cast<int>(ScanWorkerExit::unknownTarget));
};

auto tCoordinatorOk = test("HostingScan/aWorkerResultIsReportedThroughTheNoise") = []
{
    const auto folder = freshFolder("coordinatorOk");
    const auto module = makeBundle(folder / "Fine.vst3");
    const auto formats = StubFormats();

    const auto scan = scanThroughWorker(formats, module, fakeWorker("ok"));

    check(scan.succeeded());
    check(scan.path == module);
    check(scan.classes.size() == 1);
    check(scan.classes[0].name == "Fine");

    fs::remove_all(folder);
};

auto tRealWorker = test("HostingScan/theRealWorkerAnswersThroughTheProtocol") = []
{
    const auto folder = freshFolder("realWorker");
    const auto module = makeBundle(folder / "Real.vst3");
    const auto refuses = makeBundle(folder / "Refuses.vst3");
    const auto formats = StubFormats();

    const auto scan = scanThroughWorker(formats, module, fakeWorker("worker"));
    check(scan.succeeded());
    check(scan.classes.size() == 1);
    check(scan.classes[0].name == "Real");

    // A plugin that will not load is an answer, not a crash.
    const auto refused = scanThroughWorker(formats, refuses, fakeWorker("worker"));
    check(!refused.crashed);
    check(refused.error == "the stub refuses to load");

    // A target the worker cannot act on exits non-zero: a crash, with the reason.
    const auto missing = (folder / "Missing.vst3").string();
    const auto bad = scanThroughWorker(formats, missing, fakeWorker("worker"));
    check(bad.crashed);
    check(bad.error.find("exited with code 3") != std::string::npos);
    check(bad.error.find("nothing is installed") != std::string::npos);

    fs::remove_all(folder);
};

auto tCoordinatorCrashes = test("HostingScan/everyWayAWorkerFailsIsACrash") = []
{
    const auto folder = freshFolder("crashes");
    const auto module = makeBundle(folder / "Bad.vst3", 99);
    const auto formats = StubFormats();

    const auto exited = scanThroughWorker(formats, module, fakeWorker("crash"));
    check(exited.crashed);
    check(exited.error.find("exited with code 3") != std::string::npos);
    check(exited.error.find("refusing to scan") != std::string::npos);
    check(exited.stamp() == moduleStamp(module));
    check(exited.format == PluginFormat::VST3);

    const auto aborted = scanThroughWorker(formats, module, fakeWorker("abort"));
    check(aborted.crashed);
    check(aborted.stamp() == moduleStamp(module));

    const auto garbage = scanThroughWorker(formats, module, fakeWorker("garbage"));
    check(garbage.crashed);
    check(garbage.error.find("no usable result") != std::string::npos);

    const auto old = scanThroughWorker(formats, module, fakeWorker("old"));
    check(old.crashed);
    check(old.error.find("no usable result") != std::string::npos);

    auto missing = WorkerCommand();
    missing.executable = (folder / "NoSuchWorker").string();
    const auto unlaunched = scanThroughWorker(formats, module, missing);
    check(unlaunched.crashed);

    fs::remove_all(folder);
};

auto tTimeout = test("HostingScan/aHungWorkerIsKilledAtTheTimeout") = []
{
    const auto folder = freshFolder("timeout");
    const auto module = makeBundle(folder / "Hang.vst3");
    const auto formats = StubFormats();

    const auto start = Clock::now();
    const auto scan = scanThroughWorker(formats, module, fakeWorker("hang"), 300);

    check(scan.crashed);
    check(scan.error.find("killed after 300 ms") != std::string::npos);
    check(scan.stamp() == moduleStamp(module));
    check(msSince(start) < 20000);

    fs::remove_all(folder);
};

auto tLimit = test("HostingScan/thePoolNeverRunsMoreWorkersThanTheLimit") = []
{
    const auto folder = freshFolder("limit");
    const auto formats = StubFormats();

    auto targets = Vector<std::string>();

    for (auto name: {"A.vst3", "B.vst3", "C.vst3", "D.vst3"})
        targets.add(makeBundle(folder / name));

    auto coordinator = ScanCoordinator(formats);
    auto recorder = Recorder();
    recorder.listenTo(coordinator);

    coordinator.setWorkerCommand(fakeWorker("ok", "300"));
    coordinator.setMaxConcurrent(2);
    check(coordinator.maxConcurrent() == 2);

    const auto start = Clock::now();
    coordinator.start(targets);

    check(pumpUntilFinished(coordinator));
    check(recorder.scans.size() == 4);
    check(recorder.lastDone == 4);
    check(recorder.lastTotal == 4);

    // Four 300 ms workers two at a time take at least two rounds.
    check(msSince(start) >= 600);

    for (const auto& target: targets)
    {
        const auto* scan = recorder.find(target);
        check(scan != nullptr && scan->succeeded());
    }

    fs::remove_all(folder);
};

auto tCancel = test("HostingScan/cancelStopsTheOutstandingWorkers") = []
{
    const auto folder = freshFolder("cancel");
    const auto formats = StubFormats();

    auto coordinator = ScanCoordinator(formats);
    auto recorder = Recorder();
    recorder.listenTo(coordinator);

    coordinator.setWorkerCommand(fakeWorker("hang"));
    coordinator.setMaxConcurrent(2);
    coordinator.start({makeBundle(folder / "A.vst3"),
                       makeBundle(folder / "B.vst3"),
                       makeBundle(folder / "C.vst3")});

    coordinator.poll();
    check(coordinator.isRunning());

    const auto start = Clock::now();
    coordinator.cancel();

    check(!coordinator.isRunning());
    check(recorder.finished == 1);
    check(recorder.scans.empty());
    check(msSince(start) < 10000);

    fs::remove_all(folder);
};

auto tInProcess = test("HostingScan/inProcessModeScansThroughTheFormats") = []
{
    const auto folder = freshFolder("inProcess");
    const auto module = makeBundle(folder / "Local.vst3");
    const auto missing = (folder / "Missing.vst3").string();
    const auto formats = StubFormats();

    auto coordinator = ScanCoordinator(formats);
    auto recorder = Recorder();
    recorder.listenTo(coordinator);

    coordinator.setMode(ScanMode::InProcess);
    coordinator.start({module, missing});

    check(pumpUntilFinished(coordinator));
    check(recorder.scans.size() == 2);

    const auto* local = recorder.find(module);
    check(local != nullptr && local->succeeded());
    check(local->classes[0].name == "Local");

    // A bad request in process is a clean failure carrying the reason.
    const auto* absent = recorder.find(missing);
    check(absent != nullptr && !absent->crashed);
    check(!absent->error.empty());

    fs::remove_all(folder);
};

auto tEmptyRun = test("HostingScan/anEmptyRunFinishesAtOnce") = []
{
    const auto formats = StubFormats();
    auto coordinator = ScanCoordinator(formats);
    auto recorder = Recorder();
    recorder.listenTo(coordinator);

    coordinator.start({});

    check(!coordinator.isRunning());
    check(recorder.finished == 1);
    check(recorder.lastDone == 0);
    check(recorder.lastTotal == 0);
};

struct SessionRecorder
{
    void listenTo(ScanSession& session)
    {
        session.onStarted = [this](const ScanSessionPlan& planToUse)
        { plan = planToUse; };

        session.onTargetFinished =
            [this](int index, int total, const ModuleScan& scan)
        {
            lastIndex = index;
            lastTotal = total;
            scans.add(scan);
        };

        session.onFinished = [this](const ScanSessionSummary& summaryToUse)
        {
            summary = summaryToUse;
            ++finished;
        };
    }

    ScanSessionPlan plan;
    ScanSessionSummary summary;
    Vector<ModuleScan> scans;
    int lastIndex = 0;
    int lastTotal = 0;
    int finished = 0;
};

ScanSessionOptions sessionOptions(const fs::path& folder)
{
    auto options = ScanSessionOptions();
    options.cachePath = (folder / "db" / "Plugins.json").string();
    options.searchFolders = {(folder / "Plugins").string()};
    options.useDefaultFolders = false;
    options.mode = ScanMode::InProcess;
    return options;
}

// One whole sweep, pumped to the end.
SessionRecorder sweep(const FormatManager& formats, ScanSessionOptions options)
{
    auto session = ScanSession(formats, std::move(options));
    auto recorder = SessionRecorder();
    recorder.listenTo(session);

    session.start();
    check(pumpUntilFinished(session));
    check(recorder.finished == 1);

    return recorder;
}

auto tSweep = test("HostingScan/aSweepScansEverythingItFindsIntoTheCache") = []
{
    const auto folder = freshFolder("sweep");
    makeBundle(folder / "Plugins" / "A.vst3");
    makeBundle(folder / "Plugins" / "Vendor" / "B.vst3");
    makeBundle(folder / "Plugins" / "Refuses.vst3");

    const auto formats = StubFormats();
    const auto first = sweep(formats, sessionOptions(folder));

    check(first.plan.found == 3);
    check(first.plan.cached == 0);
    check(first.plan.toScan == 3);
    check(first.plan.targetsOf(PluginFormat::VST3) == 3);
    check(first.plan.folders == 1);
    check(first.plan.jobs == defaultScanJobCount());
    check(first.lastIndex == 3);
    check(first.lastTotal == 3);
    check(first.summary.scanned == 3);
    check(first.summary.ok == 2);
    check(first.summary.failed == 1);
    check(first.summary.saved);
    check(!first.summary.cancelled);

    auto cache = PluginCache(first.summary.cachePath);
    check(cache.load());
    check(cache.plugins().size() == 2);
    check(cache.searchPaths().size() == 1);

    // Nothing changed: the second sweep loads nothing, failures included.
    const auto second = sweep(formats, sessionOptions(folder));
    check(second.plan.found == 3);
    check(second.plan.cached == 3);
    check(second.plan.toScan == 0);
    check(second.summary.scanned == 0);
    check(second.summary.saved);

    // A forced sweep scans everything again.
    auto forced = sessionOptions(folder);
    forced.forceRescan = true;
    check(sweep(formats, forced).plan.toScan == 3);

    fs::remove_all(folder);
};

auto tUninstalled = test("HostingScan/anUninstalledModuleLeavesTheDatabase") = []
{
    const auto folder = freshFolder("uninstalled");
    makeBundle(folder / "Plugins" / "Kept.vst3");
    makeBundle(folder / "Plugins" / "Gone.vst3");

    const auto formats = StubFormats();
    check(sweep(formats, sessionOptions(folder)).summary.ok == 2);

    fs::remove_all(folder / "Plugins" / "Gone.vst3");

    const auto after = sweep(formats, sessionOptions(folder));
    check(after.plan.found == 1);
    check(after.plan.toScan == 0);

    auto cache = PluginCache(after.summary.cachePath);
    check(cache.load());
    check(cache.document().modules.size() == 1);
    check(cache.plugins()[0].name == "Kept");

    fs::remove_all(folder);
};

auto tSweepWorkers = test("HostingScan/aSweepThroughWorkersBlacklistsACrash") = []
{
    const auto folder = freshFolder("sweepWorkers");
    const auto good = makeBundle(folder / "Plugins" / "Good.vst3");

    const auto formats = StubFormats();

    auto options = sessionOptions(folder);
    options.mode = ScanMode::Subprocess;
    options.worker = fakeWorker("worker");
    options.jobs = 2;

    const auto first = sweep(formats, options);
    check(first.summary.ok == 1);
    check(first.plan.jobs == 2);

    // A new module whose worker crashes is recorded, stamped, and left alone
    // next time.
    const auto bad = makeBundle(folder / "Plugins" / "Bad.vst3");
    options.worker = fakeWorker("abort");

    const auto second = sweep(formats, options);
    check(second.plan.toScan == 1);
    check(second.summary.crashed == 1);
    check(second.scans[0].path == bad);

    auto cache = PluginCache(second.summary.cachePath);
    check(cache.load());
    check(cache.isBlacklisted(bad));
    check(!cache.isBlacklisted(good));

    const auto third = sweep(formats, options);
    check(third.plan.toScan == 0);

    // Rewritten, the module gets another chance.
    makeBundle(folder / "Plugins" / "Bad.vst3", 128);
    options.worker = fakeWorker("worker");
    const auto fourth = sweep(formats, options);
    check(fourth.plan.toScan == 1);
    check(fourth.summary.ok == 1);

    fs::remove_all(folder);
};

auto tSweepFormats = test("HostingScan/aSweepLeavesFormatsItDidNotLookFor") = []
{
    const auto folder = freshFolder("sweepFormats");
    makeBundle(folder / "Plugins" / "A.vst3");

    auto formats = FormatManager();
    formats.add(makeOwned<StubFileFormat>());

    auto registry = makeOwned<StubRegistryFormat>();
    auto* units = registry.get();
    units->installed["aufx:dely:Test"] = 3;
    units->installed["aumu:Synt:Test"] = 4;
    formats.add(std::move(registry));

    const auto both = sweep(formats, sessionOptions(folder));
    check(both.plan.found == 3);
    check(both.plan.targetsOf(PluginFormat::VST3) == 1);
    check(both.plan.targetsOf(PluginFormat::AU) == 2);

    // A VST3-only sweep after the units were removed keeps their entries.
    units->installed.clear();

    auto vst3Only = sessionOptions(folder);
    vst3Only.formats = {PluginFormat::VST3};

    const auto narrowed = sweep(formats, vst3Only);
    check(narrowed.plan.found == 1);
    check(narrowed.plan.targetsOf(PluginFormat::AU) == 0);

    auto cache = PluginCache(narrowed.summary.cachePath);
    check(cache.load());
    check(cache.document().modules.size() == 3);

    // A sweep of everything finds them gone.
    const auto all = sweep(formats, sessionOptions(folder));
    check(all.plan.found == 1);
    check(cache.load());
    check(cache.document().modules.size() == 1);

    fs::remove_all(folder);
};

auto tEmptyFolder = test("HostingScan/anEmptyFolderIsAGoodAnswer") = []
{
    const auto folder = freshFolder("emptyFolder");
    fs::create_directories(folder / "Plugins");

    const auto formats = StubFormats();
    const auto result = sweep(formats, sessionOptions(folder));

    check(result.plan.found == 0);
    check(result.summary.scanned == 0);
    check(result.summary.saved);
    check(fs::exists(result.summary.cachePath));

    fs::remove_all(folder);
};

auto tSessionCancel = test("HostingScan/aCancelledSweepSavesWhatItHas") = []
{
    const auto folder = freshFolder("sessionCancel");
    makeBundle(folder / "Plugins" / "A.vst3");
    makeBundle(folder / "Plugins" / "B.vst3");

    const auto formats = StubFormats();

    auto options = sessionOptions(folder);
    options.mode = ScanMode::Subprocess;
    options.worker = fakeWorker("hang");

    auto session = ScanSession(formats, options);
    auto recorder = SessionRecorder();
    recorder.listenTo(session);

    session.start();
    check(session.isRunning());

    session.cancel();
    check(!session.isRunning());
    check(recorder.finished == 1);
    check(recorder.summary.cancelled);
    check(recorder.summary.saved);
    check(recorder.summary.scanned == 0);

    fs::remove_all(folder);
};
} // namespace
} // namespace HostingScanTests
