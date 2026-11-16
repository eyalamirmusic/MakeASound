// Scanning real CLAP modules: the fixtures this tree builds (Tests/HostFixtures)
// as CLAP, in process for the well-behaved ones and through a worker for the one
// that crashes and the one that hangs, a module holding two plugins, the
// CLAP_PATH folders, and a CLAP row through the cache.

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/CLAP/CLAPFormat.h>

#include <ScanProtocol/ScanWorker.h>

#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/FilePath.h>

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>

#ifndef MAKEASOUND_HOST_FIXTURE_CLAP_DIR
#error "MAKEASOUND_HOST_FIXTURE_CLAP_DIR must name the fixtures' CLAP folder"
#endif

#ifndef MAKEASOUND_HOST_FIXTURE_CLAP_HAS_EDITOR
#error "MAKEASOUND_HOST_FIXTURE_CLAP_HAS_EDITOR must be 0 or 1"
#endif

namespace HostingCLAPScanTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;
using namespace MakeASound::ScanProtocol;

namespace
{
namespace fs = std::filesystem;

// Whether the fixtures' modules offer clap.gui: they do wherever the CLAP
// adapter is built with the plugin UI, unless a plugin says it has no editor.
constexpr auto fixturesHaveEditors = MAKEASOUND_HOST_FIXTURE_CLAP_HAS_EDITOR != 0;

// Spelled as the module walk spells what it finds: UTF-8 with '/'.
std::string fixture(const char* name)
{
    const auto folder = fs::path(MAKEASOUND_HOST_FIXTURE_CLAP_DIR);

    return eacp::FilePath(folder / (std::string(name) + ".clap")).str();
}

fs::path freshFolder(const char* name)
{
    auto folder = fs::temp_directory_path() / "MakeASoundHostingCLAPTests" / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

struct DefaultFormats : FormatManager
{
    DefaultFormats() { addDefaultFormats(); }
};

class ScopedVariable
{
public:
    ScopedVariable(const char* nameToUse, const std::string& value)
        : name(nameToUse)
        , previous(eacp::getEnv(nameToUse))
    {
        eacp::setEnv(name, value);
    }

    ~ScopedVariable()
    {
        if (previous)
            eacp::setEnv(name, *previous);
        else
            eacp::unsetEnv(name);
    }

    ScopedVariable(const ScopedVariable&) = delete;
    ScopedVariable& operator=(const ScopedVariable&) = delete;

private:
    const char* name;
    std::optional<std::string> previous;
};

WorkerCommand realWorker()
{
    auto command = WorkerCommand();
    command.executable = MAKEASOUND_FAKE_SCAN_WORKER;
    command.argumentPrefix = {"--fake-worker", "formats", std::string(scanArgument)};
    command.decode = decodeScanResult;
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

const PluginDescription* findClass(const ModuleScan& scan, const std::string& name)
{
    for (const auto& description: scan.classes)
        if (description.name == name)
            return &description;

    return nullptr;
}

auto tFormat = test("HostingCLAPScan/theFormatClaimsCLAPModules") = []
{
    const auto formats = DefaultFormats();
    const auto* clap = formats.find(PluginFormat::CLAP);

    check(clap != nullptr);

    if (clap == nullptr)
        return;

    check(formats.findForTarget("/a/Plugin.clap") == clap);
    check(formats.findForTarget("C:/a/Plugin.CLAP") == clap);
    check(formats.findForTarget("/a/Plugin.vst3") != clap);
    check(formats.findForTarget("/a/Plugin.clap.txt") == nullptr);
    check(formats.formatOfTarget("/a/Plugin.Clap") == PluginFormat::CLAP);

    for (const auto& folder: clap->defaultSearchFolders())
        check(fs::is_directory(folder));

    const auto targets = clap->findTargets({MAKEASOUND_HOST_FIXTURE_CLAP_DIR});
    check(targets.size() == 5);
    check(targets.contains(fixture("HostFixtureEffect")));
    check(targets.contains(fixture("HostFixturePair")));
    check(targets.contains(fixture("HostFixtureScanCrash")));
};

auto tClapPath = test("HostingCLAPScan/clapPathComesFirst") = []
{
    const auto root = freshFolder("clapPath");
    fs::create_directories(root / "First");
    fs::create_directories(root / "Second");

    const auto first = eacp::FilePath(root / "First").str();
    const auto second = eacp::FilePath(root / "Second").str();
    const auto missing = eacp::FilePath(root / "Missing").str();

    // ';' on Windows, ':' elsewhere, as the spec has it.
    const auto separator = fs::path::preferred_separator == '\\' ? ";" : ":";

    {
        const auto variable =
            ScopedVariable("CLAP_PATH",
                           first + separator + separator + missing + separator
                               + second + "/" + separator + first);
        const auto folders = CLAPFormat().defaultSearchFolders();

        check(folders.size() >= 2);

        if (folders.size() >= 2)
        {
            check(folders[0] == first);
            check(folders[1] == second);
        }

        check(!folders.contains(missing));

        auto count = 0;

        for (const auto& folder: folders)
            count += folder == first ? 1 : 0;

        check(count == 1);
    }

    {
        const auto variable = ScopedVariable("CLAP_PATH", "");
        const auto folders = CLAPFormat().defaultSearchFolders();

        check(!folders.contains(first));
    }

    fs::remove_all(root);
};

auto tEffect = test("HostingCLAPScan/anEffectIsDescribed") = []
{
    const auto formats = DefaultFormats();
    const auto outcome = formats.scanTarget(fixture("HostFixtureEffect"));

    check(outcome.request == TargetRequest::ok);

    const auto& scan = outcome.scan;
    check(scan.succeeded());
    check(scan.format == PluginFormat::CLAP);
    check(scan.stamp() == moduleStamp(fixture("HostFixtureEffect")));
    check(scan.stamp().fileSize > 0);
    check(scan.classes.size() == 1);

    if (scan.classes.size() != 1)
        return;

    const auto& effect = scan.classes[0];
    check(effect.format == PluginFormat::CLAP);
    check(effect.path == scan.path);
    check(effect.classId == "com.makeasound.hostfixtureeffect.HfEf");
    check(effect.name == "HostFixtureEffect");
    check(effect.vendor == "MakeASound Tests");
    check(effect.version == "1.0.0");
    check(effect.category == "Audio Effect");
    check(effect.subCategories == "audio-effect");
    check(!effect.isInstrument());
    check(effect.numAudioInputs == 3);
    check(effect.numAudioOutputs == 2);
    check(!effect.hasMidiInput);

    // GenericEditor, where the plugin UI is built.
    check(effect.hasEditor == fixturesHaveEditors);
};

auto tInstrument = test("HostingCLAPScan/anInstrumentIsDescribed") = []
{
    const auto formats = DefaultFormats();
    const auto scan = formats.scanTarget(fixture("HostFixtureInstrument")).scan;

    check(scan.succeeded());
    check(scan.classes.size() == 1);

    if (scan.classes.size() != 1)
        return;

    const auto& instrument = scan.classes[0];
    check(instrument.name == "HostFixtureInstrument");
    check(instrument.classId == "com.makeasound.hostfixtureinstrument.HfIn");
    check(instrument.category == "Instrument");
    check(instrument.subCategories == "instrument|synthesizer");
    check(instrument.isInstrument());
    check(instrument.numAudioInputs == 0);
    check(instrument.numAudioOutputs == 2);
    check(instrument.hasMidiInput);

    check(instrument.hasEditor == fixturesHaveEditors);
};

auto tPair = test("HostingCLAPScan/aModuleIsScannedAsEveryPluginItHolds") = []
{
    const auto formats = DefaultFormats();
    const auto scan = formats.scanTarget(fixture("HostFixturePair")).scan;

    check(scan.succeeded());
    check(scan.classes.size() == 2);

    const auto* stereo = findClass(scan, "HostFixturePairStereo");
    const auto* mono = findClass(scan, "HostFixturePairMono");
    check(stereo != nullptr && mono != nullptr);

    if (stereo == nullptr || mono == nullptr)
        return;

    check(stereo->path == scan.path && mono->path == scan.path);
    check(stereo->classId == "com.makeasound.hostfixturepair.HfPs");
    check(mono->classId == "com.makeasound.hostfixturepair.HfPm");
    check(stereo->numAudioInputs == 2 && stereo->numAudioOutputs == 2);
    check(mono->numAudioInputs == 1 && mono->numAudioOutputs == 1);
    check(stereo->hasEditor == fixturesHaveEditors);
    check(!mono->hasEditor);
};

auto tBroken = test("HostingCLAPScan/aModuleThatCannotLoadIsACleanError") = []
{
    const auto folder = freshFolder("broken");
    const auto broken = folder / "Broken.clap";

    // A bundle on macOS, a file elsewhere: junk either way.
    fs::create_directories(broken / "Contents" / "MacOS");

    {
        auto out = std::ofstream(broken / "Contents" / "MacOS" / "Broken");
        out << "junk";
    }

    const auto junkFile = folder / "Junk.clap";

    {
        auto out = std::ofstream(junkFile);
        out << "junk";
    }

    const auto formats = DefaultFormats();

    for (const auto& target: {broken, junkFile})
    {
        const auto outcome = formats.scanTarget(eacp::FilePath(target).str());

        check(outcome.request == TargetRequest::ok);
        check(outcome.scan.format == PluginFormat::CLAP);
        check(!outcome.scan.crashed);
        check(!outcome.scan.error.empty());
        check(outcome.scan.classes.empty());
    }

    fs::remove_all(folder);
};

auto tMissing = test("HostingCLAPScan/aMissingModuleIsNoSuchTarget") = []
{
    const auto formats = DefaultFormats();
    const auto outcome = formats.scanTarget("/nowhere/at/all/Missing.clap");

    check(outcome.request == TargetRequest::noSuchTarget);
};

auto tWorker = test("HostingCLAPScan/aWorkerDescribesWhatItLoads") = []
{
    const auto formats = DefaultFormats();
    const auto scan = scanThroughWorker(formats, fixture("HostFixturePair"));

    check(scan.succeeded());
    check(scan.format == PluginFormat::CLAP);
    check(scan.classes.size() == 2);
    check(findClass(scan, "HostFixturePairMono") != nullptr);
};

auto tCrash = test("HostingCLAPScan/aPluginThatCrashesIsRecordedAsACrash") = []
{
    const auto formats = DefaultFormats();
    const auto target = fixture("HostFixtureScanCrash");
    const auto scan = scanThroughWorker(formats, target);

    check(scan.crashed);
    check(scan.format == PluginFormat::CLAP);
    check(scan.stamp() == moduleStamp(target));
    check(scan.error.find("exited with code") != std::string::npos);
};

auto tHang = test("HostingCLAPScan/aPluginThatHangsIsKilledAtTheTimeout") = []
{
    const auto formats = DefaultFormats();
    const auto target = fixture("HostFixtureScanHang");
    const auto scan = scanThroughWorker(formats, target, 2000);

    check(scan.crashed);
    check(scan.format == PluginFormat::CLAP);
    check(scan.error.find("killed after 2000 ms") != std::string::npos);
    check(scan.stamp() == moduleStamp(target));
};

auto tSweep =
    test("HostingCLAPScan/aSweepOfTheFixturesRoundTripsThroughTheCache") = []
{
    const auto folder = freshFolder("sweep");
    const auto formats = DefaultFormats();

    auto options = ScanSessionOptions();
    options.cachePath = (folder / "Plugins.json").string();
    options.searchFolders = {MAKEASOUND_HOST_FIXTURE_CLAP_DIR};
    options.useDefaultFolders = false;
    options.formats = {PluginFormat::CLAP};
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
    check(plan.found == 5);
    check(plan.targetsOf(PluginFormat::CLAP) == 5);
    check(plan.toScan == 5);
    check(summary.ok == 3);
    check(summary.crashed == 2);
    check(summary.saved);

    // The format is written by name, as the cache writes every enum.
    {
        auto in = std::ifstream(options.cachePath);
        const auto text = std::string(std::istreambuf_iterator<char>(in), {});
        check(text.find("\"CLAP\"") != std::string::npos);
    }

    auto cache = PluginCache(options.cachePath);
    useFormatStamps(cache, formats);
    check(cache.load());

    const auto plugins = cache.plugins();
    check(plugins.size() == 4);

    for (const auto& plugin: plugins)
        check(plugin.format == PluginFormat::CLAP);

    const auto* effect = cache.find(fixture("HostFixtureEffect"));
    check(effect != nullptr);

    if (effect != nullptr)
    {
        check(effect->format == PluginFormat::CLAP);
        check(effect->succeeded());
        check(effect->classes.size() == 1);
        check(effect->classes.size() == 1
              && effect->classes[0].classId
                     == "com.makeasound.hostfixtureeffect.HfEf");
    }

    check(cache.isBlacklisted(fixture("HostFixtureScanCrash")));
    check(cache.isBlacklisted(fixture("HostFixtureScanHang")));
    check(!cache.isBlacklisted(fixture("HostFixturePair")));

    // Nothing changed, so nothing is loaded again, crashes included.
    const auto [again, againSummary] = run();
    check(again.found == 5);
    check(again.toScan == 0);
    check(againSummary.scanned == 0);

    fs::remove_all(folder);
};
} // namespace
} // namespace HostingCLAPScanTests
