// Scanning Audio Units: the identifier's spelling, the registry sweep, and
// Apple's always-present AUDelay and DLSMusicDevice described in process and
// through a worker, matched against what Tamber Plug's scanner wrote for AUDelay.
//
// The one place the tests find a component through the registry rather than
// registering their own: a scan IS a registry lookup, and Apple's units are on
// every Mac (see CLAUDE.md's test rules).

#include "HostingFixtures.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/AU/AUFormat.h>

#include <ScanProtocol/ScanWorker.h>

#include <Miro/Reflection/Serialize.h>

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <filesystem>
#include <thread>

namespace HostingAUScanTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;
using namespace MakeASound::ScanProtocol;

namespace
{
namespace fs = std::filesystem;

constexpr auto appleDelay = "aufx:dely:appl";
constexpr auto appleSynth = "aumu:dls :appl";

struct DefaultFormats : FormatManager
{
    DefaultFormats() { addDefaultFormats(); }
};

ModuleScan scanThroughWorker(const FormatManager& formats, const std::string& target)
{
    auto command = WorkerCommand();
    command.executable = MAKEASOUND_FAKE_SCAN_WORKER;
    command.argumentPrefix = {"--fake-worker", "formats", std::string(scanArgument)};
    command.decode = decodeScanResult;

    auto coordinator = ScanCoordinator(formats);
    auto scans = Vector<ModuleScan>();

    coordinator.onModuleScanned = [&scans](const ModuleScan& scan)
    { scans.add(scan); };

    coordinator.setWorkerCommand(command);
    coordinator.start({target});

    const auto start = std::chrono::steady_clock::now();

    while (coordinator.isRunning()
           && std::chrono::steady_clock::now() - start < std::chrono::seconds(60))
    {
        coordinator.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    check(scans.size() == 1);
    return scans.size() == 1 ? scans[0] : ModuleScan();
}

auto tSpelling = test("HostingAUScan/anIdentifierIsThreeFourCharacterCodes") = []
{
    check(looksLikeAudioUnitIdentifier("aufx:dely:appl"));
    check(looksLikeAudioUnitIdentifier("aumu:dls :appl"));
    check(looksLikeAudioUnitIdentifier("    :    :    "));

    check(!looksLikeAudioUnitIdentifier(""));
    check(!looksLikeAudioUnitIdentifier("aufx:dely"));
    check(!looksLikeAudioUnitIdentifier("aufx:dely:appl:"));
    check(!looksLikeAudioUnitIdentifier("aufx-dely-appl"));
    check(!looksLikeAudioUnitIdentifier("aufx:de:y:appl"));
    check(!looksLikeAudioUnitIdentifier("/Library/Audio/x.component"));
    check(!looksLikeAudioUnitIdentifier("au\x01x:dely:appl"));
    check(!looksLikeAudioUnitIdentifier("aufx:d\xc3\xa9y:appl"));
};

auto tDefaults = test("HostingAUScan/theDefaultFormatsAreVST3ThenAU") = []
{
    const auto formats = DefaultFormats();

    check(formats.size() == 2);
    check(formats[0].format() == PluginFormat::VST3);
    check(formats[1].format() == PluginFormat::AU);
    check(formats.findForTarget(appleDelay) == &formats[1]);
    check(formats.findForTarget("/a/aufx:dely:ap.vst3") == &formats[0]);
    check(formats[1].defaultSearchFolders().empty());
};

auto tRegistry = test("HostingAUScan/theRegistrySweepFindsApplesUnits") = []
{
    const auto format = AUFormat();
    const auto targets = format.findTargets({});

    check(targets.contains(appleDelay));
    check(targets.contains(appleSynth));

    for (const auto& target: targets)
    {
        check(looksLikeAudioUnitIdentifier(target));
        check(target.starts_with("aufx") || target.starts_with("aumu")
              || target.starts_with("aumf") || target.starts_with("aumi"));
    }
};

auto tDelay = test("HostingAUScan/anEffectIsDescribedAsPlugDescribedIt") = []
{
    const auto formats = DefaultFormats();
    const auto outcome = formats.scanTarget(appleDelay);

    check(outcome.request == TargetRequest::ok);

    const auto& scan = outcome.scan;
    check(scan.succeeded());
    check(scan.format == PluginFormat::AU);
    check(scan.fileSize == 0);
    check(scan.lastModified > 0);
    check(scan.stamp() == formats[1].stamp(appleDelay));
    check(scan.classes.size() == 1);

    const auto plug = Miro::createFromJSON<ModuleScan>(
        Miro::Json::parse(std::string(HostingFixtures::appleDelayScan)));

    const auto& ours = scan.classes[0];
    const auto& theirs = plug.classes[0];

    check(ours.format == theirs.format);
    check(ours.path == theirs.path);
    check(ours.classId == theirs.classId);
    check(ours.name == theirs.name);
    check(ours.vendor == theirs.vendor);
    check(ours.category == theirs.category);
    check(ours.subCategories == theirs.subCategories);
    check(ours.numAudioInputs == theirs.numAudioInputs);
    check(ours.numAudioOutputs == theirs.numAudioOutputs);
    check(ours.hasMidiInput == theirs.hasMidiInput);
    check(ours.hasEditor == theirs.hasEditor);
    check(!ours.version.empty());
};

auto tSynth = test("HostingAUScan/anInstrumentIsDescribed") = []
{
    const auto formats = DefaultFormats();
    const auto scan = formats.scanTarget(appleSynth).scan;

    check(scan.succeeded());
    check(scan.classes.size() == 1);

    const auto& synth = scan.classes[0];
    check(synth.vendor == "Apple");
    check(synth.category == "aumu");
    check(synth.isInstrument());
    check(synth.hasMidiInput);
    check(synth.numAudioOutputs >= 2);
};

auto tMissing = test("HostingAUScan/anUnregisteredIdentifierIsABadRequest") = []
{
    const auto formats = DefaultFormats();
    const auto outcome = formats.scanTarget("aufx:Nope:MkTs");

    check(outcome.request == TargetRequest::noSuchTarget);
    check(formats[1].stamp("aufx:Nope:MkTs") == ModuleStamp());
    check(!formats[1].targetExists("aufx:Nope:MkTs"));
    check(formats[1].targetExists(appleDelay));
};

auto tWorker = test("HostingAUScan/aWorkerDescribesAnAudioUnit") = []
{
    const auto formats = DefaultFormats();
    const auto scan = scanThroughWorker(formats, appleDelay);

    check(scan.succeeded());
    check(scan.format == PluginFormat::AU);
    check(scan.stamp() == formats[1].stamp(appleDelay));
    check(scan.classes.size() == 1);
    check(scan.classes[0].name == "AUDelay");
};

auto tBlacklist = test("HostingAUScan/aCrashedAudioUnitStaysBlacklisted") = []
{
    const auto formats = DefaultFormats();

    auto crashed = crashedScan(
        appleDelay, "killed", formats.stampTarget(appleDelay), PluginFormat::AU);

    const auto folder = fs::temp_directory_path() / "MakeASoundHostingAUTests";
    fs::remove_all(folder);

    auto cache = PluginCache((folder / "blacklist" / "Plugins.json").string());
    useFormatStamps(cache, formats);
    cache.apply(crashed);

    check(cache.isBlacklisted(appleDelay));
    check(cache.modulesNeedingScan({appleDelay}).empty());

    // Stamped from another version, it is due for another try.
    crashed.lastModified += 1;
    cache.apply(crashed);
    check(!cache.isBlacklisted(appleDelay));
};
} // namespace
} // namespace HostingAUScanTests
