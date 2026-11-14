// Off Windows a path's key is its text, byte for byte: case, '\', repeated and
// trailing separators and a "//?/" prefix all keep two texts apart, as they did
// before Windows folded its spellings together, and an Audio Unit's id is its
// own key. HostingPathKeyTests-Windows.cpp holds the Windows side.

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/PathText.h>

#include <NanoTest/NanoTest.h>

#include <string>

namespace HostingPathKeyTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;

using MakeASound::Hosting::PluginDescription;

namespace
{
ModuleScan scanOf(const std::string& path)
{
    auto scan = ModuleScan();
    scan.path = path;

    auto description = PluginDescription();
    description.name = "Spelled";
    description.path = path;
    description.classId = "SPEL";
    scan.classes.add(description);

    return scan;
}

auto tKey = test("HostingPathKey/theKeyIsTheTextItself") = []
{
    for (const auto* text: {"/Library/Audio/Plug-Ins/VST3/A.vst3",
                            "/library/audio/plug-ins/vst3/a.vst3",
                            "/Library//Audio/Plug-Ins/VST3/A.vst3",
                            "/Library/Audio/Plug-Ins/VST3/",
                            "C:\\VST3\\A.vst3",
                            "c:/VST3/A.vst3",
                            "//?/C:/x",
                            "//server/share/A.vst3",
                            "aufx:mrev:appl",
                            "/VST3/\xff.vst3",
                            "/VST3/\xf0\x90\x90\xa8.vst3",
                            ""})
        check(pathKey(text) == text);
};

auto tCache = test("HostingPathKey/caseAndSeparatorsStillSeparateEntries") = []
{
    auto cache = PluginCache("unused.json");
    cache.apply(scanOf("/Library/Audio/Plug-Ins/VST3/A.vst3"));

    check(cache.find("/Library/Audio/Plug-Ins/VST3/A.vst3") != nullptr);
    check(cache.find("/library/audio/plug-ins/vst3/a.vst3") == nullptr);
    check(cache.find("/Library/Audio/Plug-Ins/VST3//A.vst3") == nullptr);
    check(cache.find("/Library/Audio/Plug-Ins/VST3/A.vst3/") == nullptr);

    cache.apply(scanOf("/Library/Audio/Plug-Ins/VST3/a.vst3"));
    check(cache.document().modules.size() == 2);
    check(cache.plugins().size() == 2);
};
} // namespace
} // namespace HostingPathKeyTests
