// Id paths whose ids hold a '/' (IdPath.h): inside one id the path doubles it,
// so a path names one parameter, while an id without one, and so every host id,
// AUv3 identifier and golden manifest of such ids, is as it was. The saved
// document keeps each id as written. The TamberPlug case is Plug's Vocoder's
// shape: a "Dry/Wet" leaf at the root and inside a mounted group.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <Miro/Json.h>
#include <NanoTest/NanoTest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace IdPathTests
{
using namespace nano;
using namespace MakeASound;

namespace
{
constexpr auto v1 = ParameterOptions {.versionHint = 1};

bool near(float a, float b)
{
    return std::abs(a - b) < 1e-5f;
}

bool splitsTo(std::string_view path, const std::vector<std::string>& ids)
{
    auto split = splitIdPath(path);

    if (split.size() != static_cast<int>(ids.size()))
        return false;

    for (auto i = 0; i < split.size(); ++i)
        if (split[i] != ids[static_cast<size_t>(i)])
            return false;

    return true;
}

std::string joined(const std::vector<std::string>& ids)
{
    auto path = std::string {};

    for (const auto& id: ids)
        appendIdSegment(path, id);

    return path;
}

// --- The Vocoder's shape --------------------------------------------------------

struct CarrierParams : ParameterGroup
{
    CarrierParams()
        : ParameterGroup("Carrier")
    {
        add(level, mix);
    }

    PercentParam level {"Level", 0.8f, v1};
    PercentParam mix {"Dry/Wet", 1.f, v1};
};

struct VocoderParams : ParameterGroup
{
    VocoderParams() { add(bands, mix, level, carrier); }

    ChoiceParam bands {"Bands", {"8", "12", "16", "24", "32"}, 3, v1};
    PercentParam mix {"Dry/Wet", 1.f, v1};
    PercentParam level {"Level", 0.8f, v1};
    CarrierParams carrier;
};

// A release after the port adds a leaf that holds a '/' too.
struct LaterVocoderParams : VocoderParams
{
    LaterVocoderParams() { add(blend); }

    PercentParam blend {"Wet/Dry", 0.5f, {.versionHint = 2}};
};

template <typename ParamsT>
struct VocoderPort : StatePlugin<State<ParamsT>>
{
    std::string_view name() const override { return "Vocoder Port"; }
    BusLayout getBusLayout() const override { return BusLayout::stereoOut(); }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}
};

template <typename P>
PluginDescription plugPort(std::optional<int> indexedThrough = {})
{
    return {.name = "Vocoder Port",
            .category = Category::Instrument,
            .pluginCode = "Plgv",
            .create = [] { return OwningPointer<Plugin>(EA::makeOwned<P>()); },
            .idScheme = IdScheme::TamberPlug,
            .overrides = {.indexedThroughVersion = indexedThrough}};
}

OwningPointer<PluginWrapper> wrap(const PluginDescription& description)
{
    return EA::makeOwned<PluginWrapper>(description.create(),
                                        PluginFormat::Unknown,
                                        parameterIdentityFor(description));
}

// A leaf and a group-and-leaf that spell the same once joined with '/'.
struct LookalikeGroup : ParameterGroup
{
    LookalikeGroup()
        : ParameterGroup("Dry")
    {
        add(wet);
    }

    FloatParam wet {"Wet", 0.f, 1.f, 0.25f};
};

struct LookalikeParams : ParameterGroup
{
    LookalikeParams() { add(mix, dry); }

    FloatParam mix {"Dry/Wet", 0.f, 1.f, 0.75f};
    LookalikeGroup dry;
};

struct EdgeParams : ParameterGroup
{
    EdgeParams() { add(trailing, plain); }

    FloatParam trailing {"Send/", 0.f, 1.f, 0.f};
    FloatParam plain {"Plain", 0.f, 1.f, 0.f};
};

// --- Cases ----------------------------------------------------------------------

auto tPlain = test("IdPath/anIdWithoutASlashIsItsOwnSegment") = []
{
    check(escapeIdSegment("Cutoff") == "Cutoff");
    check(escapeIdSegment("Osc 1") == "Osc 1");
    check(escapeIdSegment("") == "");
    check(joined({"Osc 1", "Filter", "Cutoff"}) == "Osc 1/Filter/Cutoff");
    check(splitsTo("Osc 1/Filter/Cutoff", {"Osc 1", "Filter", "Cutoff"}));
    check(splitsTo("Gain", {"Gain"}));
    check(splitIdPath("").empty());

    // What every id without a '/' hashed to before, so to its host id.
    check(hostIdFor(joined({"Filter", "Cutoff"})) == hostIdFor("Filter/Cutoff"));
};

auto tDoubled = test("IdPath/aSlashInsideAnIdIsDoubled") = []
{
    check(escapeIdSegment("Dry/Wet") == "Dry//Wet");
    check(escapeIdSegment("a//b") == "a////b");
    check(joined({"Dry/Wet"}) == "Dry//Wet");
    check(joined({"Mix", "Dry/Wet"}) == "Mix/Dry//Wet");
    check(joined({"In/Out", "Dry/Wet"}) == "In//Out/Dry//Wet");
    check(joined({"Dry", "Wet"}) == "Dry/Wet");
};

auto tRoundTrip = test("IdPath/splitReadsBackWhatAppendWrote") = []
{
    auto cases = std::vector<std::vector<std::string>> {
        {"Gain"},
        {"Dry/Wet"},
        {"Carrier", "Dry/Wet"},
        {"In/Out", "Mid/Side", "Dry/Wet"},
        {"a//b", "c"},
        {"Osc 1", "Unison", "Detune"},
    };

    for (const auto& ids: cases)
        check(splitsTo(joined(ids), ids));
};

auto tEdges = test("IdPath/onlyAnEdgeSlashCannotBeDelimited") = []
{
    check(isDelimitableId("Dry/Wet"));
    check(isDelimitableId("Gain"));
    check(isDelimitableId(""));
    check(!isDelimitableId("Send/"));
    check(!isDelimitableId("/Send"));
    check(!isDelimitableId("/"));

    auto params = EdgeParams {};
    auto list = ParameterList(params);
    auto problems = list.identityProblems();

    check(problems.size() == 1);
    check(!problems.empty() && problems[0].find("\"Send/\"") != std::string::npos);
};

auto tLookalike = test("IdPath/aLeafAndAGroupThatSpellTheSameStayApart") = []
{
    auto params = LookalikeParams {};
    auto list = ParameterList(params);

    check(list.entry(0).id == "Dry//Wet");
    check(list.entry(1).id == "Dry/Wet");
    check(list.indexOf("Dry//Wet") == 0);
    check(list.indexOf("Dry/Wet") == 1);
    check(list.entry(0).hostId != list.entry(1).hostId);
    check(list.entry(1).hostId == hostIdFor("Dry/Wet"));
    check(list.identityProblems().empty());
};

auto tState = test("IdPath/theDocumentKeepsEachIdAsWritten") = []
{
    auto plugin = VocoderPort<LookalikeParams> {};
    plugin.params.mix.setValue(0.5f);
    plugin.params.dry.wet.setValue(1.f);

    auto document = plugin.saveState(StateContext::Session);
    auto parsed = Miro::Json::getParsedValue(document);
    const auto& saved = parsed["params"];

    check(saved["Dry/Wet"].isNumber());
    check(saved["Dry"]["Wet"].isNumber());
    check(document.find("Dry//Wet") == std::string::npos);

    auto next = VocoderPort<LookalikeParams> {};
    next.loadState(document, StateContext::Session);

    check(near(next.params.mix.getValue(), 0.5f));
    check(near(next.params.dry.wet.getValue(), 1.f));
};

auto tPlug = test("TamberPlug/aSlashInALeafIdMovesNoIndexAndNoKey") = []
{
    auto wrapper = wrap(plugPort<VocoderPort<VocoderParams>>());
    const auto& list = wrapper->plugin().parameters();

    // Plug's flat leaf order, each id its index, as Plug's wrappers gave them.
    auto paths = std::vector<std::string_view> {
        "Bands", "Dry//Wet", "Level", "Carrier/Level", "Carrier/Dry//Wet"};

    check(list.size() == static_cast<int>(paths.size()));

    for (auto i = 0; i < static_cast<int>(paths.size()); ++i)
    {
        const auto& entry = list.entry(i);
        check(entry.id == paths[static_cast<size_t>(i)]);
        check(list.indexOf(entry.id) == i);
        check(entry.hostId == static_cast<uint32_t>(i));
        check(entry.address == static_cast<uint64_t>(i));
        check(entry.identifier == entry.id);
    }

    check(list.entry(4).groups.size() == 1);
    check(list.entry(4).groups[0].id == "Carrier");
    check(list.entry(4).param->id() == "Dry/Wet");
    check(list.identityProblems().empty());
};

auto tPlugDocument = test("TamberPlug/plugsDryWetKeyLoadsAndSavesAsPlugWroteIt") = []
{
    // A Plug document's shape: each mounted list a sub-object, the leaf's own id
    // its key, choices by name.
    constexpr auto plugDocument = std::string_view {
        R"({"params":{"Bands":"16","Carrier":{"Dry/Wet":0.75,"Level":0.3},"Dry/Wet":0.25,"Level":0.5},"version":1})"};

    auto port = VocoderPort<VocoderParams> {};
    port.loadState(plugDocument, StateContext::Session);

    check(port.params.bands.getIndex() == 2);
    check(near(port.params.mix.getValue(), 0.25f));
    check(near(port.params.level.getValue(), 0.5f));
    check(near(port.params.carrier.mix.getValue(), 0.75f));
    check(near(port.params.carrier.level.getValue(), 0.3f));

    auto saved = Miro::Json::getParsedValue(port.saveState(StateContext::Session));
    const auto& params = saved["params"];

    check(params["Dry/Wet"].isNumber());
    check(params["Carrier"]["Dry/Wet"].isNumber());
    check(params.asObject().size() == 4);
    check(params["Carrier"].asObject().size() == 2);
};

auto tPostPort = test("TamberPlug/aPostPortLeafWithASlashHashesItsPath") = []
{
    auto wrapper = wrap(plugPort<VocoderPort<LaterVocoderParams>>(1));
    const auto& list = wrapper->plugin().parameters();
    auto blend = list.indexOf("Wet//Dry");

    check(blend == 5);
    check(list.entry(blend).postPort);
    check(list.entry(blend).hostId == hostIdFor("Wet//Dry"));

    // The indexed leaves keep theirs.
    for (auto i = 0; i < 5; ++i)
        check(list.entry(i).hostId == static_cast<uint32_t>(i));

    check(list.identityProblems().empty());
};
} // namespace
} // namespace IdPathTests
