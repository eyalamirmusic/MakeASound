// The identity manifests: describeIdentity over every test plugin under every
// IdScheme it can ship under, against the goldens in Tests/Identity/. A diff is a
// change to an id some host has stored; make it deliberately, then rewrite the
// goldens with MAKEASOUND_UPDATE_IDENTITY=1 and review them.

#include "IdentityManifest.h"
#include "JucePort.h"
#include "TamberPlugFixtures.h"
#include "TestPlugins.h"

#include <NanoTest/NanoTest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace IdentityManifest
{

namespace
{
std::string_view schemeName(IdScheme scheme)
{
    switch (scheme)
    {
        case IdScheme::MakeASound:
            return "MakeASound";
        case IdScheme::JUCE:
            return "JUCE";
        case IdScheme::JUCELegacyIndex:
            return "JUCELegacyIndex";
    }

    return "?";
}

std::string_view categoryName(Category category)
{
    switch (category)
    {
        case Category::Effect:
            return "Effect";
        case Category::Instrument:
            return "Instrument";
        case Category::MidiEffect:
            return "MidiEffect";
    }

    return "?";
}

std::string groupsOf(const ParameterList::Entry& entry)
{
    auto text = std::string {};

    for (const auto& group: entry.groups)
        text += (text.empty() ? "" : " ")
                + print("[%s|%s]", group.id.c_str(), group.identifier.c_str());

    return text;
}

// The plugin's description and the ids its list computes for every entry.
void describeCore(const ModuleDescription&,
                  const PluginDescription& plugin,
                  Lines& out)
{
    const auto& juce = plugin.juce;
    out.push_back(print("core juce studioOne %d legacyAutomation %d "
                        "replaceVst2 %d midiControllers %d",
                        juce.studioOneCompatibleParameters ? 1 : 0,
                        juce.forceLegacyParameterAutomationType ? 1 : 0,
                        juce.vst3CanReplaceVst2 ? 1 : 0,
                        vst3MidiControllersFor(plugin) ? 1 : 0));

    auto wrapper = PluginWrapper(
        plugin.create(), PluginFormat::Unknown, parameterIdentityFor(plugin));
    const auto& list = wrapper.plugin().parameters();

    for (auto index = 0; index < list.size(); ++index)
    {
        const auto& entry = list.entry(index);
        out.push_back(print(
            "core entry %d \"%s\" key \"%s\" hint %d automatable %d session %d "
            "bypass %s hostId %u address %llu identifier \"%s\" aaxId \"%s\" "
            "groups {%s}",
            index,
            entry.id.c_str(),
            entry.hostKey.c_str(),
            entry.param->versionHint(),
            entry.automatable ? 1 : 0,
            entry.sessionOnly ? 1 : 0,
            entry.addedBypass ? "added" : (entry.bypass ? "declared" : "no"),
            static_cast<unsigned>(entry.hostId),
            static_cast<unsigned long long>(entry.address),
            entry.identifier.c_str(),
            entry.aaxId.c_str(),
            groupsOf(entry).c_str()));
    }
}

template <typename P>
PluginCreateFn creator()
{
    return [] { return OwningPointer<Plugin>(EA::makeOwned<P>()); };
}

// One golden file: a module and every description of it to describe.
struct Case
{
    std::string_view file;
    ModuleDescription module;
};

PluginDescription describedAs(PluginDescription plugin, IdScheme scheme)
{
    plugin.idScheme = scheme;
    return plugin;
}

ModuleDescription moduleOf(FourCC manufacturer,
                           const PluginDescription& plugin,
                           std::initializer_list<IdScheme> schemes)
{
    auto module = ModuleDescription {};
    module.vendor = "MakeASound Identity";
    module.manufacturerCode = manufacturer;

    for (auto scheme: schemes)
        module.plugins.add(describedAs(plugin, scheme));

    return module;
}

constexpr auto everyScheme = {
    IdScheme::MakeASound, IdScheme::JUCE, IdScheme::JUCELegacyIndex};

ModuleDescription testPluginModule(PluginDescription plugin)
{
    return moduleOf("MaSo", plugin, everyScheme);
}

Vector<Case> cases()
{
    using namespace TestPlugins;

    auto result = Vector<Case> {};

    result.add({"Gain",
                testPluginModule({.name = "Gain",
                                  .pluginCode = "Gain",
                                  .create = creator<GainPlugin>()})});

    result.add({"NewerGain",
                testPluginModule({.name = "Newer Gain",
                                  .pluginCode = "Gnw2",
                                  .create = creator<NewerGainPlugin>()})});

    result.add({"UnbypassedGain",
                testPluginModule({.name = "Unbypassed Gain",
                                  .pluginCode = "Gnub",
                                  .create = creator<UnbypassedGainPlugin>()})});

    // Its MIDI input makes it 'aumf'; once more with MIDI controllers on.
    auto latent = testPluginModule(
        {.name = "Latent", .pluginCode = "Latn", .create = creator<LatentEffect>()});

    for (auto scheme: everyScheme)
        latent.plugins.add({.name = "Latent CC",
                            .pluginCode = "Lcc1",
                            .create = creator<LatentEffect>(),
                            .idScheme = scheme,
                            .vst3MidiControllers = true});

    result.add({"Latent", latent});

    result.add({"Synth",
                testPluginModule({.name = "Synth",
                                  .category = Category::Instrument,
                                  .pluginCode = "Echo",
                                  .create = creator<SynthPlugin>()})});

    // JucePort's two descriptions, the same plugin under MakeASound's ids, and
    // JUCE's with its other settings flipped.
    auto port = ModuleDescription {};
    port.vendor = "MakeASound Identity";
    port.manufacturerCode = JucePort::manufacturer;
    port.plugins.add(
        describedAs(JucePort::description(IdScheme::JUCE), IdScheme::MakeASound));
    port.plugins.add(JucePort::description(IdScheme::JUCE));
    port.plugins.add(JucePort::description(IdScheme::JUCELegacyIndex));

    auto flipped = JucePort::description(IdScheme::JUCE);
    flipped.name = "Port Flipped";
    flipped.pluginCode = "Jflp";
    flipped.juce = {.studioOneCompatibleParameters = false,
                    .forceLegacyParameterAutomationType = true,
                    .vst3CanReplaceVst2 = false};
    port.plugins.add(flipped);
    port.plugins.add(describedAs(flipped, IdScheme::JUCELegacyIndex));
    result.add({"JucePort", port});

    // Tamber Plug's examples as ported, under Plug's manufacturer code. Their
    // leaf keys repeat (SVFFilter's bands), so no JUCE scheme.
    auto plug = ModuleDescription {};
    plug.vendor = "MakeASound Identity";
    plug.manufacturerCode = "EAPM";
    plug.plugins.add({.name = "SVFFilter",
                      .pluginCode = "Svf1",
                      .create = creator<TamberPlugFixtures::SVFPort>()});
    plug.plugins.add({.name = "Chorus",
                      .pluginCode = "Chr1",
                      .create = creator<TamberPlugFixtures::ChorusPort>()});
    result.add({"TamberPlug", plug});

    return result;
}

std::string describeModuleIdentity(const ModuleDescription& module)
{
    auto text = std::string {};

    for (const auto& plugin: module.plugins)
        text += describeIdentity(module, plugin);

    return text;
}

std::string goldenPath(std::string_view file)
{
    return std::string {MAKEASOUND_TEST_IDENTITY_DIR} + "/" + std::string {file}
           + ".txt";
}

std::vector<std::string> describedLines(const std::string& text)
{
    auto lines = std::vector<std::string> {};
    auto stream = std::istringstream {text};

    for (auto line = std::string {}; std::getline(stream, line);)
        if (isDescribedHere(line))
            lines.push_back(line);

    return lines;
}

bool matchesGolden(const Case& golden)
{
    auto actual = describeModuleIdentity(golden.module);
    auto path = goldenPath(golden.file);

    if (std::getenv("MAKEASOUND_UPDATE_IDENTITY") != nullptr)
    {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << actual;
        return true;
    }

    auto file = std::ifstream(path, std::ios::binary);
    auto expected = std::string {std::istreambuf_iterator<char>(file), {}};

    auto want = describedLines(expected);
    auto got = describedLines(actual);

    if (want == got)
        return true;

    std::fprintf(stderr,
                 "Identity manifest %s differs from %s\n",
                 golden.file.data(),
                 path.c_str());

    for (auto line = size_t {0}; line < std::max(want.size(), got.size()); ++line)
    {
        auto expectedLine = line < want.size() ? want[line] : "<none>";
        auto actualLine = line < got.size() ? got[line] : "<none>";

        if (expectedLine != actualLine)
            std::fprintf(stderr,
                         "line %d\n  golden: %s\n  actual: %s\n",
                         static_cast<int>(line + 1),
                         expectedLine.c_str(),
                         actualLine.c_str());
    }

    return false;
}

bool matchesGolden(std::string_view file)
{
    for (const auto& golden: cases())
        if (golden.file == file)
            return matchesGolden(golden);

    return false;
}
} // namespace

std::vector<Section>& sections()
{
    static auto all = std::vector<Section> {{{"plugin", "core"}, describeCore}};
    return all;
}

bool addSection(Section section)
{
    sections().push_back(std::move(section));
    return true;
}

std::string describeIdentity(const ModuleDescription& module,
                             const PluginDescription& plugin)
{
    auto lines = Lines {};
    lines.push_back(print("plugin \"%.*s\" code %s scheme %.*s category %.*s",
                          static_cast<int>(plugin.name.size()),
                          plugin.name.data(),
                          fourCC(plugin.pluginCode.toUint32()).c_str(),
                          static_cast<int>(schemeName(plugin.idScheme).size()),
                          schemeName(plugin.idScheme).data(),
                          static_cast<int>(categoryName(plugin.category).size()),
                          categoryName(plugin.category).data()));

    for (const auto& section: sections())
        section.describe(module, plugin, lines);

    auto text = std::string {};

    for (const auto& line: lines)
        text += line + "\n";

    return text;
}

bool isDescribedHere(std::string_view line)
{
    auto tag = line.substr(0, line.find(' '));

    for (const auto& section: sections())
        for (auto known: section.tags)
            if (known == tag)
                return true;

    return false;
}

std::string fourCC(uint32_t code)
{
    auto text = std::string(4, ' ');

    for (auto i = 0; i < 4; ++i)
        text[static_cast<size_t>(i)] = static_cast<char>(code >> (24 - 8 * i));

    return text;
}

} // namespace IdentityManifest

namespace
{
using namespace nano;

auto tGain = test("Identity/gainMatchesItsManifest") = []
{ check(IdentityManifest::matchesGolden("Gain")); };

auto tNewerGain = test("Identity/newerGainMatchesItsManifest") = []
{ check(IdentityManifest::matchesGolden("NewerGain")); };

auto tUnbypassed = test("Identity/unbypassedGainMatchesItsManifest") = []
{ check(IdentityManifest::matchesGolden("UnbypassedGain")); };

auto tLatent = test("Identity/latentMatchesItsManifest") = []
{ check(IdentityManifest::matchesGolden("Latent")); };

auto tSynth = test("Identity/synthMatchesItsManifest") = []
{ check(IdentityManifest::matchesGolden("Synth")); };

auto tJucePort = test("Identity/jucePortMatchesItsManifest") = []
{ check(IdentityManifest::matchesGolden("JucePort")); };

auto tTamberPlug = test("Identity/tamberPlugPortsMatchTheirManifest") = []
{ check(IdentityManifest::matchesGolden("TamberPlug")); };

// CI builds Release, where nothing asserts: every plugin a manifest describes
// is checked here as well.
auto tNoProblems = test("Identity/noTestPluginHasAnIdentityProblem") = []
{
    for (const auto& golden: IdentityManifest::cases())
    {
        for (const auto& plugin: golden.module.plugins)
        {
            auto wrapper =
                MakeASound::PluginWrapper(plugin.create(),
                                          MakeASound::PluginFormat::Unknown,
                                          MakeASound::parameterIdentityFor(plugin));
            auto problems = wrapper.plugin().parameters().identityProblems();

            for (const auto& problem: problems)
                std::fprintf(
                    stderr, "%s: %s\n", plugin.name.data(), problem.c_str());

            check(problems.empty());
        }
    }
};

struct ClashingParams : MakeASound::ParameterGroup
{
    ClashingParams() { add(first, second); }

    MakeASound::FloatParam first {
        "First", 0.f, 1.f, 0.f, {.hostId = 7u, .versionHint = 1}};
    MakeASound::FloatParam second {
        "Second", 0.f, 1.f, 0.f, {.hostId = 7u, .versionHint = 1}};
};

auto tHostIdClash = test("Identity/aHostIdClashIsAProblem") = []
{
    auto params = ClashingParams {};
    auto list = MakeASound::ParameterList {params};
    auto problems = list.identityProblems();

    // The host id, and the AUv3 address that is the host id.
    check(problems.size() == 2);
    check(problems[0].find("share host id 7") != std::string::npos);
    check(problems[1].find("an AUv3 address") != std::string::npos);
};

struct ReservedParams : MakeASound::ParameterGroup
{
    ReservedParams() { add(high); }

    MakeASound::FloatParam high {
        "High", 0.f, 1.f, 0.f, {.hostId = 0x80000001u, .versionHint = 1}};
};

auto tReserved = test("Identity/aPinBeyondThirtyOneBitsIsAProblem") = []
{
    using MakeASound::IdScheme;

    auto params = ReservedParams {};
    auto list = MakeASound::ParameterList {params};
    check(list.identityProblems().size() == 1);

    list.setIdentity({IdScheme::JUCE, false});
    check(list.identityProblems().empty());
};

// JUCE's own duplicate check: one key twice, the host ids pinned apart.
struct RepeatedKeyParams : MakeASound::ParameterGroup
{
    RepeatedKeyParams() { add(first, second); }

    MakeASound::FloatParam first {
        "First", 0.f, 1.f, 0.f, {.hostId = 1u, .hostKey = "gain", .versionHint = 1}};
    MakeASound::FloatParam second {
        "Second",
        0.f,
        1.f,
        0.f,
        {.hostId = 2u, .hostKey = "gain", .versionHint = 1}};
};

auto tRepeatedKey = test("Identity/aRepeatedJuceKeyIsAProblemUnderJuceOnly") = []
{
    auto params = RepeatedKeyParams {};
    auto list = MakeASound::ParameterList {params};
    check(list.identityProblems().empty());

    auto juce = MakeASound::ParameterList {
        params, MakeASound::ParameterIdentity {MakeASound::IdScheme::JUCE}};
    auto problems = juce.identityProblems();

    // The key, the AUv3 address hashed from it, and the AAX id that is the key.
    check(problems.size() == 3);
};

// A golden names every format's lines, so a build without some format still
// compares the rest; this one compares at least the core's.
auto tSections = test("Identity/theCoreSectionIsAlwaysDescribed") = []
{
    check(IdentityManifest::isDescribedHere("core entry 0"));
    check(IdentityManifest::isDescribedHere("plugin \"Gain\""));
    check(!IdentityManifest::isDescribedHere("nosuchformat param 0"));
};
} // namespace
