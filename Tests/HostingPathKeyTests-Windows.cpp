// On Windows one module has many spellings: '\' or '/' between its folders, in
// any mix, repeated or trailing separators, a "\\?\" long-path prefix, and any
// case, since NTFS and the Win32 APIs ignore case. A cache an older build wrote
// recorded what std::filesystem::path::string() gave, '\' and mixed, the default
// search folders are recorded with '/', and a host looks a module up by whatever
// text it holds (typed by a user, saved by a JUCE build). Each of these is one
// entry, never two.

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/PathText.h>

#include <Miro/Reflection/Serialize.h>

#include <eacp/Core/Utils/FilePath.h>

#include <NanoTest/NanoTest.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

namespace HostingPathKeyTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;

using MakeASound::Hosting::PluginDescription;

namespace
{
namespace fs = std::filesystem;

fs::path freshFolder(const char* name)
{
    auto folder = fs::temp_directory_path() / "MakeASoundHostingPathKeyTests" / name;
    auto ec = std::error_code();
    fs::remove_all(folder, ec);
    fs::create_directories(folder);
    return folder;
}

void makeBundle(const fs::path& path)
{
    fs::create_directories(path / "Contents" / "x86_64-win");
    auto out = std::ofstream(path / "Contents" / "x86_64-win" / "binary.vst3",
                             std::ios::binary | std::ios::trunc);
    out << std::string(64, 'x');
}

ModuleScan scanOf(const std::string& path, bool succeeded = true)
{
    auto scan = ModuleScan();
    scan.format = PluginFormat::VST3;
    scan.path = path;
    scan.setStamp(moduleStamp(path));

    if (succeeded)
    {
        auto description = PluginDescription();
        description.name = "Spelled";
        description.path = path;
        description.classId = "SPEL";
        scan.classes.add(description);
    }
    else
    {
        scan.error = "initialize failed";
    }

    return scan;
}

std::string withLowerDrive(std::string text)
{
    text[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    return text;
}

// ASCII only, which is all a test folder under the scratch profile holds.
std::string upperCased(std::string text)
{
    for (auto& character: text)
        character =
            static_cast<char>(std::toupper(static_cast<unsigned char>(character)));

    return text;
}

std::string readFile(const fs::path& path)
{
    auto in = std::ifstream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

int occurrences(const std::string& text, const std::string& part)
{
    auto count = 0;

    for (auto at = text.find(part); at != std::string::npos;
         at = text.find(part, at + part.size()))
        ++count;

    return count;
}

auto tKeyCase = test("HostingPathKey/theKeyIgnoresCaseThroughout") = []
{
    check(pathKey("c:/vst3/vendor/spelled.vst3")
          == pathKey("C:/VST3/Vendor/Spelled.vst3"));
    check(pathKey("C:\\Program Files\\Common Files\\VST3\\A.vst3")
          == pathKey("c:/PROGRAM FILES/common files/vst3/a.VST3"));

    // Beyond ASCII too: "Caf\u00e9/\u00e9" and "CAF\u00c9/\u00c9", and Cyrillic
    // "\u0434" and "\u0414".
    check(pathKey("C:/Caf\xc3\xa9/\xc3\xa9.vst3")
          == pathKey("C:/CAF\xc3\x89/\xc3\x89.vst3"));
    check(pathKey("C:/\xd0\xb4.vst3") == pathKey("C:/\xd0\x94.vst3"));

    check(pathKey("C:/VST3/A.vst3") != pathKey("C:/VST3/B.vst3"));
    check(pathKey("C:/VST3/A.vst3") != pathKey("D:/VST3/A.vst3"));
};

auto tKeySeparators =
    test("HostingPathKey/theKeyIgnoresRepeatedAndTrailingSeparators") = []
{
    check(pathKey("C:\\VST3\\\\A.vst3") == pathKey("C:/VST3/A.vst3"));
    check(pathKey("C:/VST3//Vendor///A.vst3") == pathKey("C:/VST3/Vendor/A.vst3"));
    check(pathKey("C:\\VST3\\") == pathKey("C:/VST3"));
    check(pathKey("C:/VST3//") == pathKey("C:\\VST3"));

    // A drive's root keeps its separator: "C:" alone is that drive's current
    // folder, a different place.
    check(pathKey("C:\\") == pathKey("c:/"));
    check(pathKey("C:\\\\") == pathKey("C:/"));
    check(pathKey("C:/") != pathKey("C:"));
    check(pathKey("/") == pathKey("\\"));

    check(pathKey("C:/VST3/A.vst3") != pathKey("C:/VST3A.vst3"));
};

auto tKeyLongPath = test("HostingPathKey/aLongPathPrefixKeysAsThePathItNames") = []
{
    check(pathKey("\\\\?\\C:\\x") == pathKey("C:\\x"));
    check(pathKey("\\\\?\\c:\\VST3\\A.vst3") == pathKey("C:/VST3/A.vst3"));
    check(pathKey("//?/C:/VST3/A.vst3") == pathKey("C:/VST3/A.vst3"));
    check(pathKey("\\\\?\\UNC\\Server\\Share\\A.vst3")
          == pathKey("\\\\server\\share\\A.vst3"));
    check(pathKey("\\\\?\\unc\\server\\share\\A.vst3")
          == pathKey("//server/share/A.vst3"));

    check(pathKey("\\\\?\\C:\\x") != pathKey("C:\\y"));
};

auto tKeyUnc = test("HostingPathKey/aUncPathKeepsItsLeadingDoubleSeparator") = []
{
    check(pathKey("\\\\Server\\Share\\VST3\\A.vst3")
          == pathKey("//server/share//vst3/a.vst3"));
    check(pathKey("\\\\server\\share\\") == pathKey("//server/share"));

    // Collapsing must not turn a share into a folder at the current drive's
    // root.
    check(pathKey("\\\\server\\share\\A.vst3")
          != pathKey("\\server\\share\\A.vst3"));
    check(pathKey("//server/share/A.vst3") != pathKey("/server/share/A.vst3"));
    check(pathKey("//server/share/A.vst3").starts_with("//"));
};

// Text that is not valid UTF-8 (a path an ANSI conversion produced) is folded in
// its ASCII bytes only: decoding it would turn every bad byte into U+FFFD, and
// two different texts into one key.
auto tKeyInvalidUtf8 = test("HostingPathKey/theKeyKeepsInvalidUtf8BytesApart") = []
{
    check(pathKey("C:/\xff.vst3") != pathKey("C:/\xfe.vst3"));
    check(pathKey("C:/\xff.vst3") != pathKey("C:/\xef\xbf\xbd.vst3"));
    check(pathKey("C:/Caf\xe9.vst3") != pathKey("C:/Caf\xc9.vst3"));

    check(pathKey("c:\\vst3\\\\a\xff.vst3\\") == pathKey("C:/VST3/A\xff.VST3"));
    check(pathKey("C:/VST3/A\xff.VST3").find('\xff') != std::string::npos);
};

// NTFS folds case one UTF-16 unit at a time, so a character beyond the BMP (a
// surrogate pair) keeps its case there: Deseret "\U00010428" and "\U00010400"
// are two files. The key keeps them apart, and still folds what is around them.
auto tKeySurrogates =
    test("HostingPathKey/theKeyLeavesCharactersBeyondTheBmpAlone") = []
{
    constexpr auto lower = "\xf0\x90\x90\xa8"; // U+10428
    constexpr auto upper = "\xf0\x90\x90\x80"; // U+10400

    check(pathKey(std::string("C:/") + lower + ".vst3")
          != pathKey(std::string("C:/") + upper + ".vst3"));
    check(pathKey(std::string("c:/a") + lower + "b.vst3")
          == pathKey(std::string("C:/A") + lower + "B.VST3"));
    check(pathKey(std::string("C:/") + lower).ends_with(lower));
};

// A key's key is itself, for every input, malformed prefixes included, and only
// "\\?\X:" and "\\?\UNC\" are long-path prefixes: "\\?\" alone is not the empty
// text, and "\\?\UNC" is not a relative "UNC".
auto tKeyIdempotent =
    test("HostingPathKey/theKeyOfAKeyIsItselfEvenForOddPrefixes") = []
{
    for (const auto* text: {"///?/C:/x",
                            "\\\\\\?\\C:\\x",
                            "\\\\?\\UNC\\?\\C:\\x",
                            "\\\\?\\\\C:\\x",
                            "//?/",
                            "\\\\?\\",
                            "//?/UNC",
                            "\\\\?\\UNC\\",
                            "\\\\?\\C:\\",
                            "\\\\?\\C:",
                            "C:/VST3//A.vst3/",
                            "//server//share/"})
    {
        const auto key = pathKey(text);
        check(pathKey(key) == key);
    }

    check(pathKey("//?/") != pathKey(""));
    check(pathKey("\\\\?\\") != pathKey(""));
    check(pathKey("//?/UNC") != pathKey("UNC"));
    check(pathKey("\\\\?\\\\C:\\x") == pathKey("C:/x"));
    check(pathKey("\\\\?\\C:\\") == pathKey("C:/"));
    check(pathKey("///?/C:/x") == pathKey("C:/x"));
};

auto tFind = test("HostingPathKey/aCacheFindsAnEntryByAnySpelling") = []
{
    auto cache = PluginCache("unused.json");
    cache.apply(scanOf("c:\\VST3\\Vendor/Spelled.vst3"));

    check(cache.find("C:/VST3/Vendor/Spelled.vst3") != nullptr);
    check(cache.find("C:\\VST3\\Vendor\\Spelled.vst3") != nullptr);
    check(cache.find("c:/VST3\\Vendor/Spelled.vst3") != nullptr);
    check(cache.find("C:/VST3/Vendor/spelled.vst3") != nullptr);
    check(cache.find("C:/vst3/VENDOR/SPELLED.VST3") != nullptr);
    check(cache.find("C:\\VST3\\\\Vendor\\Spelled.vst3\\") != nullptr);
    check(cache.find("\\\\?\\C:\\VST3\\Vendor\\Spelled.vst3") != nullptr);

    check(cache.find("C:/VST3/Spelled.vst3") == nullptr);
    check(cache.find("D:/VST3/Vendor/Spelled.vst3") == nullptr);
};

auto tApply =
    test("HostingPathKey/applyReplacesTheEntryWrittenWithAnotherSpelling") = []
{
    auto cache = PluginCache("unused.json");
    cache.apply(scanOf("C:\\VST3\\Spelled.vst3"));
    cache.apply(scanOf("c:/VST3/Spelled.vst3", false));

    check(cache.document().modules.size() == 1);
    check(cache.document().modules[0].path == "c:/VST3/Spelled.vst3");
    check(!cache.document().modules[0].succeeded());

    cache.apply(scanOf("C:/vst3//spelled.VST3/"));
    check(cache.document().modules.size() == 1);
    check(cache.document().modules[0].path == "C:/vst3//spelled.VST3/");
    check(cache.document().modules[0].succeeded());
};

auto tRemoveMissing =
    test("HostingPathKey/removeMissingKeepsAnEntryFoundUnderAnotherSpelling") = []
{
    auto cache = PluginCache("unused.json");
    cache.apply(scanOf("C:\\VST3\\Kept.vst3"));
    cache.apply(scanOf("C:\\VST3\\Gone.vst3"));

    cache.removeMissing({"c:/VST3/Kept.vst3"});
    check(cache.document().modules.size() == 1);
    check(cache.find("C:/VST3/Kept.vst3") != nullptr);

    cache.removeMissing({"C:/VST3/Kept.vst3"}, PluginFormat::VST3);
    check(cache.document().modules.size() == 1);

    cache.removeMissing({"c:/vst3/KEPT.vst3"});
    check(cache.document().modules.size() == 1);

    cache.removeMissing({"\\\\?\\C:\\VST3\\\\kept.vst3\\"}, PluginFormat::VST3);
    check(cache.document().modules.size() == 1);

    cache.removeMissing({}, PluginFormat::VST3);
    check(cache.document().modules.empty());
};

auto tStamps =
    test("HostingPathKey/aKnownModuleIsNotRescannedUnderAnotherSpelling") = []
{
    const auto folder = freshFolder("stamps");
    const auto good = folder / "Good.vst3";
    const auto bad = folder / "Bad.vst3";
    makeBundle(good);
    makeBundle(bad);

    auto cache = PluginCache("unused.json");
    cache.apply(scanOf(eacp::FilePath(good).nativeString()));
    cache.apply(scanOf(eacp::FilePath(bad).nativeString(), false));

    const auto goodText = withLowerDrive(eacp::FilePath(good).str());
    const auto badText = withLowerDrive(eacp::FilePath(bad).str());

    check(cache.modulesNeedingScan({goodText, badText}).empty());
    check(cache.isBlacklisted(badText));

    const auto goodUpper = upperCased(eacp::FilePath(good).str());
    const auto badUpper = upperCased(eacp::FilePath(bad).str());

    check(cache.modulesNeedingScan({goodUpper, badUpper}).empty());
    check(cache.isBlacklisted(badUpper));

    auto ec = std::error_code();
    fs::remove_all(folder, ec);
};

auto tWalk = test("HostingPathKey/overlappingSearchPathsFindAModuleOnce") = []
{
    const auto root = freshFolder("walk");
    const auto module = root / "Once.vst3";
    makeBundle(module);

    // A search path that is a module counts as one by its own text, so these
    // reach the result without the walk's visited set in between.
    const auto slashes = eacp::FilePath(module).str();
    const auto modules =
        findModulesWithExtension({eacp::FilePath(module).nativeString(),
                                  slashes,
                                  withLowerDrive(slashes),
                                  upperCased(slashes),
                                  eacp::FilePath(root).nativeString() + "\\"},
                                 ".vst3");

    check(modules.size() == 1);
    check(!modules.empty() && modules[0] == slashes);

    auto ec = std::error_code();
    fs::remove_all(root, ec);
};

// What a cache from before one key per module can hold: the walk spelled a module
// "C:/.../VST3\X.vst3" and a host's own lookup "C:\...\X.vst3", and each became an
// entry of its own. Loading keeps one and drops the rest, so the module is
// listed, found and evicted once, and the next save writes it once.
auto tOldDuplicates =
    test("HostingPathKey/anOldCacheWithTwoSpellingsOfOneModuleLoadsAsOne") = []
{
    const auto folder = freshFolder("oldDuplicates");
    const auto file = folder / "Plugins.json";

    const auto walked =
        std::string("C:/Program Files/Common Files/VST3\\Twice.vst3");
    const auto described =
        std::string("C:\\Program Files\\Common Files\\VST3\\Twice.vst3");
    const auto typed = std::string("c:/program files/common files/vst3/twice.vst3");
    const auto other = std::string("C:/Program Files/Common Files/VST3/Other.vst3");

    {
        auto old = PluginCacheDocument();
        old.searchPaths.add("C:/Program Files/Common Files/VST3");
        old.modules.add(scanOf(walked));
        old.modules.add(scanOf(other));
        old.modules.add(scanOf(described, false));
        old.modules.add(scanOf(typed));

        auto out = std::ofstream(file, std::ios::binary | std::ios::trunc);
        out << Miro::toJSONString(old, 2);
    }

    auto cache = PluginCache(eacp::FilePath(file).str());
    check(cache.load());

    const auto& modules = cache.document().modules;
    check(modules.size() == 2);
    check(!modules.empty() && modules[0].path == walked);
    check(modules.size() > 1 && modules[1].path == other);
    check(cache.plugins().size() == 2);

    // The first entry is the one that was already answering: find returned it
    // before, and still does, by every spelling.
    check(!modules.empty() && cache.find(described) == &modules[0]);
    check(!modules.empty() && cache.find(typed) == &modules[0]);
    check(!modules.empty() && modules[0].succeeded());

    cache.removeMissing({described, other});
    check(cache.document().modules.size() == 2);

    check(cache.save());

    const auto written = readFile(file);
    check(occurrences(written, "Twice.vst3") == 2); // its path, its class's path
    check(occurrences(written, "twice.vst3") == 0);
    check(occurrences(written, "Other.vst3") == 2);

    auto reloaded = PluginCache(eacp::FilePath(file).str());
    check(reloaded.load());
    check(reloaded.document().modules.size() == 2);
    check(reloaded.plugins().size() == 2);

    auto ec = std::error_code();
    fs::remove_all(folder, ec);
};

// A plugin that crashed the scan worker under the walk's spelling but described
// fine in the host's own process under its spelling: the old cache listed its
// classes from the second entry. Loading keeps the entry that scanned, wherever
// it is, so the plugin stays listed and is not blacklisted.
auto tOldFailureFirst =
    test("HostingPathKey/anOldCacheKeepsTheSpellingThatScannedOverAFailure") = []
{
    const auto folder = freshFolder("oldFailureFirst");
    const auto file = folder / "Plugins.json";

    const auto walked = std::string("C:/Program Files/Common Files/VST3\\Kept.vst3");
    const auto described =
        std::string("C:\\Program Files\\Common Files\\VST3\\Kept.vst3");
    const auto other = std::string("C:/Program Files/Common Files/VST3/Other.vst3");

    {
        auto old = PluginCacheDocument();
        old.modules.add(scanOf(walked, false));
        old.modules.add(scanOf(other));
        old.modules.add(scanOf(described));

        auto out = std::ofstream(file, std::ios::binary | std::ios::trunc);
        out << Miro::toJSONString(old, 2);
    }

    auto cache = PluginCache(eacp::FilePath(file).str());
    check(cache.load());

    const auto& modules = cache.document().modules;
    check(modules.size() == 2);

    // Where the module first appeared, under the spelling that scanned.
    check(!modules.empty() && modules[0].path == described);
    check(!modules.empty() && modules[0].succeeded());
    check(modules.size() > 1 && modules[1].path == other);
    check(cache.plugins().size() == 2);
    check(!cache.isBlacklisted(walked));
    check(!cache.isBlacklisted(described));

    auto ec = std::error_code();
    fs::remove_all(folder, ec);
};
} // namespace
} // namespace HostingPathKeyTests
