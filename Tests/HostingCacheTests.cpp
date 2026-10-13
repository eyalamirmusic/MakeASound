// The scan cache: a database Tamber Plug wrote, read and written back unchanged;
// stamps and the blacklist; per-format eviction; the per-app default location;
// the search-box filter; and the module walk and stamp the cache keys on.

#include "HostingFixtures.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <Miro/Reflection/Serialize.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;

namespace
{
namespace fs = std::filesystem;

// A fresh folder per case: ctest runs cases in parallel processes.
fs::path freshFolder(const char* name)
{
    auto folder = fs::temp_directory_path() / "MakeASoundHostingCacheTests" / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

// A bundle-shaped module with a binary of `bytes` bytes. The size is what a
// rewrite changes: mtimes have one-second granularity on some filesystems.
void makeBundle(const fs::path& path, std::size_t bytes = 64)
{
    fs::create_directories(path / "Contents" / "MacOS");
    auto out = std::ofstream(path / "Contents" / "MacOS" / "binary",
                             std::ios::binary | std::ios::trunc);
    out << std::string(bytes, 'x');
}

void makeFile(const fs::path& path, std::size_t bytes = 8)
{
    fs::create_directories(path.parent_path());
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    out << std::string(bytes, 'x');
}

PluginDescription makeDescription(std::string name,
                                  std::string path,
                                  std::string classId)
{
    auto description = PluginDescription();
    description.name = std::move(name);
    description.path = std::move(path);
    description.classId = std::move(classId);
    description.vendor = "MakeASound";
    description.version = "1.2.3";
    description.category = "Audio Module Class";
    description.subCategories = "Instrument|Synth";
    description.numAudioOutputs = 2;
    description.hasMidiInput = true;
    description.hasEditor = true;
    return description;
}

// A successful scan of the module at `path`, stamped from the real file.
ModuleScan makeScan(const std::string& path, const PluginDescription& description)
{
    auto scan = ModuleScan();
    scan.path = path;
    scan.setStamp(moduleStamp(path));
    scan.classes.add(description);
    return scan;
}

ModuleScan makeFailure(const std::string& path, bool crashed)
{
    auto scan = ModuleScan();
    scan.path = path;
    scan.setStamp(moduleStamp(path));
    scan.crashed = crashed;
    scan.error = crashed ? "the scan worker was killed" : "initialize failed";
    return scan;
}

PluginCacheDocument plugDocument()
{
    return Miro::createFromJSON<PluginCacheDocument>(
        Miro::Json::parse(std::string(HostingFixtures::pluginCacheDocument)));
}

auto tPlugReads = test("HostingCache/aDatabasePlugWroteLoadsUnchanged") = []
{
    const auto doc = plugDocument();

    check(doc.version == 1);
    check(doc.searchPaths.size() == 1);
    check(doc.modules.size() == 5);

    const auto& broken = doc.modules[0];
    check(broken.format == PluginFormat::VST3);
    check(broken.path.ends_with("/Broken.vst3"));
    check(broken.classes.empty());
    check(!broken.crashed);
    check(broken.error.find("Broken.vst3") != std::string::npos);
    check(broken.stamp() == ModuleStamp {1791931959, 5});

    const auto& gain = doc.modules[2];
    check(gain.succeeded());
    check(gain.classes.size() == 2);
    check(gain.classes[0].name == "Gain");
    check(gain.classes[0].classId == "4541504D4761696E5653543300000000");
    check(gain.classes[0].category == "Audio Module Class");
    check(gain.classes[0].numAudioInputs == 2);
    check(gain.classes[0].numAudioOutputs == 2);
    check(gain.classes[0].hasMidiInput);
    check(gain.classes[0].hasEditor);
    check(!gain.classes[0].isInstrument());
    check(gain.classes[1].name == "Not Gain");

    const auto& synth = doc.modules[1];
    check(synth.classes[0].subCategories == "Instrument|Synth");
    check(synth.classes[0].isInstrument());

    const auto& delay = doc.modules[4];
    check(delay.crashed);
    check(delay.classes.empty());
    check(delay.error == "the scan worker was killed after 1 ms");
    check(delay.stamp() == ModuleStamp {1791931959, 5185568});
};

auto tPlugBytes = test("HostingCache/aDatabasePlugWroteIsWrittenBackByteForByte") = []
{
    // The same keys, order, indent and number forms: a file written here is the
    // file Plug writes, so Plug reads it back.
    check(Miro::toJSONString(plugDocument(), 2)
          == HostingFixtures::pluginCacheDocument);
};

auto tPlugFile = test("HostingCache/thePlugDatabaseLoadsAndSavesThroughTheCache") = []
{
    const auto folder = freshFolder("plugFile");
    const auto path = folder / "Plugins.json";

    {
        auto out = std::ofstream(path, std::ios::binary);
        out << HostingFixtures::pluginCacheDocument;
    }

    auto cache = PluginCache(path.string());
    check(cache.load());

    const auto plugins = cache.plugins();
    check(plugins.size() == 4);
    check(plugins[0].name == "Gain");
    check(plugins[1].name == "MidiTranspose");
    check(plugins[2].name == "Not Gain");
    check(plugins[3].name == "Synth");

    check(cache.save());

    auto in = std::ifstream(path, std::ios::binary);
    const auto written = std::string(std::istreambuf_iterator<char>(in), {});
    check(written == HostingFixtures::pluginCacheDocument);

    fs::remove_all(folder);
};

auto tPlugAudioUnit = test("HostingCache/anAudioUnitEntryPlugWroteDecodes") = []
{
    const auto scan = Miro::createFromJSON<ModuleScan>(
        Miro::Json::parse(std::string(HostingFixtures::appleDelayScan)));

    check(scan.format == PluginFormat::AU);
    check(scan.path == "aufx:dely:appl");
    check(scan.stamp() == ModuleStamp {67072, 0});
    check(scan.classes.size() == 1);
    check(scan.classes[0].format == PluginFormat::AU);
    check(scan.classes[0].classId == "aufx:dely:appl");
    check(scan.classes[0].vendor == "Apple");
    check(scan.classes[0].name == "AUDelay");

    check(Miro::toJSONString(scan) == HostingFixtures::appleDelayScan);
};

auto tNoFormat = test("HostingCache/anEntryWithoutAFormatReadsAsVST3") = []
{
    const auto scan = Miro::createFromJSON<ModuleScan>(
        Miro::Json::parse(R"({"path":"/a/Old.vst3","classes":[]})"));

    check(scan.format == PluginFormat::VST3);
    check(scan.path == "/a/Old.vst3");
};

auto tRoundTrip = test("HostingCache/aCacheRoundTripsThroughItsFile") = []
{
    const auto folder = freshFolder("roundTrip");
    const auto module = (folder / "Fake.vst3").string();
    makeBundle(module, 128);

    const auto cachePath = (folder / "nested" / "Plugins.json").string();

    {
        auto cache = PluginCache(cachePath);
        cache.setSearchPaths({folder.string()});
        cache.apply(makeScan(module, makeDescription("Fake", module, "ABCD")));
        check(cache.save());
    }

    check(fs::exists(cachePath));

    auto reloaded = PluginCache(cachePath);
    check(reloaded.load());
    check(reloaded.searchPaths().size() == 1);
    check(reloaded.searchPaths()[0] == folder.string());

    const auto plugins = reloaded.plugins();
    check(plugins.size() == 1);
    check(plugins[0].name == "Fake");
    check(plugins[0].classId == "ABCD");
    check(plugins[0].isInstrument());
    check(reloaded.modulesNeedingScan({module}).empty());

    fs::remove_all(folder);
};

auto tSorted = test("HostingCache/pluginsAreFlattenedAndSorted") = []
{
    const auto folder = freshFolder("sorted");
    const auto first = (folder / "A.vst3").string();
    const auto second = (folder / "B.vst3").string();
    makeBundle(first);
    makeBundle(second);

    auto cache = PluginCache((folder / "Plugins.json").string());

    auto twoClasses = makeScan(first, makeDescription("Zebra", first, "Z1"));
    twoClasses.classes.add(makeDescription("Alpha", first, "A1"));
    cache.apply(twoClasses);
    cache.apply(makeScan(second, makeDescription("Middle", second, "M1")));

    const auto plugins = cache.plugins();
    check(plugins.size() == 3);
    check(plugins[0].name == "Alpha");
    check(plugins[1].name == "Middle");
    check(plugins[2].name == "Zebra");

    fs::remove_all(folder);
};

auto tStale = test("HostingCache/onlyNewOrChangedModulesNeedScanning") = []
{
    const auto folder = freshFolder("stale");
    const auto known = (folder / "Known.vst3").string();
    const auto changed = (folder / "Changed.vst3").string();
    const auto fresh = (folder / "Fresh.vst3").string();
    makeBundle(known, 100);
    makeBundle(changed, 100);
    makeBundle(fresh, 100);

    auto cache = PluginCache((folder / "Plugins.json").string());
    cache.apply(makeScan(known, makeDescription("Known", known, "K1")));
    cache.apply(makeScan(changed, makeDescription("Changed", changed, "C1")));

    makeBundle(changed, 200);

    const auto needed = cache.modulesNeedingScan({known, changed, fresh});
    check(needed.size() == 2);
    check(needed.contains(changed));
    check(needed.contains(fresh));

    fs::remove_all(folder);
};

auto tBlacklist = test("HostingCache/aFailureIsBlacklistedUntilItsStampChanges") = []
{
    const auto folder = freshFolder("blacklist");
    const auto crashed = (folder / "Crash.vst3").string();
    const auto failed = (folder / "Fail.vst3").string();
    makeBundle(crashed, 64);
    makeBundle(failed, 64);

    auto cache = PluginCache((folder / "Plugins.json").string());
    cache.apply(makeFailure(crashed, true));
    cache.apply(makeFailure(failed, false));

    check(cache.isBlacklisted(crashed));
    check(cache.isBlacklisted(failed));
    check(cache.modulesNeedingScan({crashed, failed}).empty());
    check(cache.plugins().empty());

    // A new build of the plugin gets a second chance.
    makeBundle(crashed, 640);
    check(!cache.isBlacklisted(crashed));
    check(cache.modulesNeedingScan({crashed, failed}).size() == 1);

    fs::remove_all(folder);
};

auto tBlacklistReload = test("HostingCache/theBlacklistSurvivesASaveAndLoad") = []
{
    const auto folder = freshFolder("blacklistReload");
    const auto module = (folder / "Bad.vst3").string();
    const auto cachePath = (folder / "Plugins.json").string();
    makeBundle(module, 64);

    {
        auto cache = PluginCache(cachePath);
        cache.apply(makeFailure(module, false));
        check(cache.save());
    }

    auto reloaded = PluginCache(cachePath);
    check(reloaded.load());
    check(reloaded.isBlacklisted(module));

    const auto* entry = reloaded.find(module);
    check(entry != nullptr);
    check(entry->error == "initialize failed");

    fs::remove_all(folder);
};

auto tRemoveMissing = test("HostingCache/removeMissingDropsUninstalledModules") = []
{
    const auto folder = freshFolder("removeMissing");
    const auto kept = (folder / "Kept.vst3").string();
    const auto gone = (folder / "Gone.vst3").string();
    makeBundle(kept);
    makeBundle(gone);

    auto cache = PluginCache((folder / "Plugins.json").string());
    cache.apply(makeScan(kept, makeDescription("Kept", kept, "K1")));
    cache.apply(makeScan(gone, makeDescription("Gone", gone, "G1")));

    cache.removeMissing({kept});
    check(cache.plugins().size() == 1);
    check(cache.plugins()[0].name == "Kept");
    check(cache.find(gone) == nullptr);

    fs::remove_all(folder);
};

auto tRemoveByFormat = test("HostingCache/removeMissingByFormatSparesOtherFormats") = []
{
    auto cache = PluginCache((freshFolder("removeByFormat") / "x.json").string());

    auto vst3 = ModuleScan();
    vst3.path = "/gone/Gone.vst3";

    auto audioUnit = ModuleScan();
    audioUnit.format = PluginFormat::AU;
    audioUnit.path = "aufx:dely:appl";

    cache.apply(vst3);
    cache.apply(audioUnit);

    // A VST3-only sweep found nothing: its own entry goes, the AU stays.
    cache.removeMissing({}, PluginFormat::VST3);
    check(cache.find("/gone/Gone.vst3") == nullptr);
    check(cache.find("aufx:dely:appl") != nullptr);

    cache.removeMissing({});
    check(cache.document().modules.empty());
};

auto tOnChanged = test("HostingCache/onChangedFiresOnlyForRealChanges") = []
{
    const auto folder = freshFolder("onChanged");
    const auto module = (folder / "Fake.vst3").string();
    makeBundle(module, 8);

    auto cache = PluginCache((folder / "Plugins.json").string());
    auto changes = 0;
    cache.onChanged = [&changes] { ++changes; };

    cache.setSearchPaths({folder.string()});
    check(changes == 1);

    cache.setSearchPaths({folder.string()});
    check(changes == 1);

    cache.apply(makeScan(module, makeDescription("Fake", module, "F1")));
    check(changes == 2);

    cache.removeMissing({module});
    check(changes == 2);

    cache.removeMissing({});
    check(changes == 3);

    fs::remove_all(folder);
};

auto tCorrupt = test("HostingCache/aCorruptFileReadsAsEmpty") = []
{
    const auto folder = freshFolder("corrupt");
    const auto cachePath = (folder / "Plugins.json").string();

    {
        auto out = std::ofstream(cachePath);
        out << R"({"version": 1, "modules": [ truncated)";
    }

    auto cache = PluginCache(cachePath);
    check(!cache.load());
    check(cache.document().modules.empty());
    check(cache.document().searchPaths.empty());

    auto missing = PluginCache((folder / "Missing.json").string());
    check(!missing.load());
    check(missing.plugins().empty());

    fs::remove_all(folder);
};

auto tClear = test("HostingCache/clearRemovesTheFileAndTheDocument") = []
{
    const auto folder = freshFolder("clear");
    const auto cachePath = (folder / "Plugins.json").string();

    auto cache = PluginCache(cachePath);
    cache.setSearchPaths({folder.string()});
    cache.apply(makeFailure((folder / "A.vst3").string(), true));
    check(cache.save());

    auto changes = 0;
    cache.onChanged = [&changes] { ++changes; };

    check(cache.clear());
    check(!fs::exists(cachePath));
    check(cache.document().modules.empty());
    check(cache.searchPaths().empty());
    check(changes == 1);

    // Clearing a cache that was never saved still succeeds, quietly.
    check(cache.clear());
    check(changes == 1);

    fs::remove_all(folder);
};

auto tStampFunction = test("HostingCache/theStampFunctionDecidesStaleness") = []
{
    auto cache = PluginCache((freshFolder("stampFunction") / "x.json").string());

    auto audioUnit = ModuleScan();
    audioUnit.format = PluginFormat::AU;
    audioUnit.path = "aumu:Synt:Test";
    audioUnit.lastModified = 100;
    audioUnit.crashed = true;
    cache.apply(audioUnit);

    // By default an identifier stamps as a file that is not there: stale, and
    // never blacklisted.
    check(!cache.isBlacklisted(audioUnit.path));

    auto version = std::int64_t {100};
    cache.setStampFunction([&version](const ModuleScan&)
                           { return ModuleStamp {version, 0}; });

    check(cache.isBlacklisted(audioUnit.path));
    check(cache.modulesNeedingScan({audioUnit.path}).empty());

    version = 101;
    check(!cache.isBlacklisted(audioUnit.path));
    check(cache.modulesNeedingScan({audioUnit.path}).size() == 1);

    cache.setStampFunction({});
    check(!cache.isBlacklisted(audioUnit.path));
};

auto tDefaultPath = test("HostingCache/theDefaultDatabaseIsPerApp") = []
{
    const auto path = fs::path(defaultPluginCachePath("My Host"));

    check(path.filename() == "Plugins.json");
    check(path.parent_path().filename() == "My Host");
    check(path.string().find("Tamber") == std::string::npos);

    const auto withCompany = fs::path(defaultPluginCachePath("My Host", "Acme"));
    check(withCompany.parent_path().filename() == "My Host");
    check(withCompany.parent_path().parent_path().filename() == "Acme");

    check(fs::path(defaultPluginCachePath()).filename() == "Plugins.json");
};

PluginDescription filterable(std::string name,
                             std::string vendor,
                             std::string subCategories,
                             std::string path)
{
    auto description = PluginDescription();
    description.name = std::move(name);
    description.vendor = std::move(vendor);
    description.subCategories = std::move(subCategories);
    description.path = std::move(path);
    return description;
}

Vector<PluginDescription> filterList()
{
    return {filterable("Pigments", "Arturia", "Instrument|Synth", "/V/Pigments.vst3"),
            filterable("Pro-Q 3", "FabFilter", "Fx|EQ", "/V/FabFilter Pro-Q 3.vst3"),
            filterable("Mini V", "Arturia", "Instrument|Synth", "/V/Mini V.vst3"),
            filterable("Rev", "Arturia", "Fx|Reverb", "/V/Rev.vst3")};
}

auto tFilterBlank = test("HostingCache/aBlankFilterMatchesEverything") = []
{
    check(filterPlugins(filterList(), "").size() == 4);
    check(filterPlugins(filterList(), "   \t ").size() == 4);
};

auto tFilterFields = test("HostingCache/theFilterMatchesNameVendorCategoryOrPath") = []
{
    check(filterPlugins(filterList(), "pigments").size() == 1);
    check(filterPlugins(filterList(), "FABFILTER").size() == 1);
    check(filterPlugins(filterList(), "reverb").size() == 1);
    check(filterPlugins(filterList(), "mini v.vst3").size() == 1);
    check(filterPlugins(filterList(), "steinberg").empty());
    check(filterPlugins(filterList(), "/V/").size() == 4);
};

auto tFilterTokens = test("HostingCache/everyFilterTokenHasToMatchInOrder") = []
{
    const auto synths = filterPlugins(filterList(), "arturia   synth");

    check(synths.size() == 2);
    check(synths[0].name == "Pigments");
    check(synths[1].name == "Mini V");
    check(filterPlugins(filterList(), "arturia eq").empty());
};

auto tInstrument = test("HostingCache/instrumentIsACaseInsensitiveSubstring") = []
{
    check(filterable("", "", "Instrument|Synth", "").isInstrument());
    check(filterable("", "", "fx|instrument", "").isInstrument());
    check(!filterable("", "", "Fx|Delay", "").isInstrument());
};

auto tWalkDepth = test("HostingCache/theWalkFindsModulesAtAnyDepthButNotInside") = []
{
    const auto root = freshFolder("walkDepth");
    makeBundle(root / "Top.vst3");
    makeBundle(root / "Vendor" / "Deep" / "Nested.vst3");
    makeBundle(root / "Top.vst3" / "Contents" / "Helper.vst3");
    makeFile(root / "Bare.VST3");
    makeFile(root / "Readme.txt");

    const auto modules = findModulesWithExtension({root.string()}, ".vst3");

    check(modules.size() == 3);
    check(modules.contains((root / "Top.vst3").string()));
    check(modules.contains((root / "Vendor" / "Deep" / "Nested.vst3").string()));
    check(modules.contains((root / "Bare.VST3").string()));

    fs::remove_all(root);
};

auto tWalkPaths = test("HostingCache/theWalkTakesModulesAndToleratesMissingPaths") = []
{
    const auto root = freshFolder("walkPaths");
    const auto module = root / "Direct.vst3";
    makeBundle(module);

    const auto modules = findModulesWithExtension(
        {module.string(), root.string(), (root / "Missing").string(), ""}, ".vst3");

    // Found twice (named, and under its folder): reported once, sorted.
    check(modules.size() == 1);
    check(modules[0] == module.string());

    fs::remove_all(root);
};

auto tWalkSymlinks = test("HostingCache/theWalkFollowsSymlinksWithoutLooping") = []
{
    const auto root = freshFolder("walkSymlinks");
    makeBundle(root / "Real" / "Linked.vst3");

    auto ec = std::error_code();
    fs::create_directory_symlink(root / "Real", root / "Link", ec);
    fs::create_directory_symlink(root, root / "Real" / "Loop", ec);

    if (ec)
        return;

    auto linked = 0;

    for (const auto& module: findModulesWithExtension({root.string()}, ".vst3"))
        if (module.ends_with("Linked.vst3"))
            ++linked;

    check(linked == 1);

    fs::remove_all(root);
};

auto tStamp = test("HostingCache/aBundleStampsFromItsBinary") = []
{
    const auto root = freshFolder("stamp");
    const auto bundle = root / "Stamped.vst3";
    makeBundle(bundle, 100);
    makeFile(bundle / "Contents" / "Resources" / "Samples.bin", 5000);

    const auto stamp = moduleStamp(bundle.string());
    check(stamp.fileSize == 100);
    check(stamp.lastModified != 0);

    makeBundle(bundle, 300);
    check(moduleStamp(bundle.string()).fileSize == 300);

    makeFile(root / "Plain.vst3", 42);
    check(moduleStamp((root / "Plain.vst3").string()).fileSize == 42);

    check(moduleStamp((root / "Missing.vst3").string()) == ModuleStamp());
    check(moduleStamp("") == ModuleStamp());

    fs::remove_all(root);
};
} // namespace
