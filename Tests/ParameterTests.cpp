// The parameter types and the registry a parameter group builds: ids, display
// names and host ids from the registration, and every type's plain, normalized
// and text conversions.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace nano;

using MakeASound::BoolParam;
using MakeASound::ChoiceParam;
using MakeASound::DecibelParam;
using MakeASound::FloatParam;
using MakeASound::FloatRange;
using MakeASound::hostIdFor;
using MakeASound::HzParam;
using MakeASound::IntParam;
using MakeASound::Parameter;
using MakeASound::ParameterGroup;
using MakeASound::ParameterList;
using MakeASound::PercentParam;
using MakeASound::TimeParam;

// A Parameter in a MIRO_REFLECT list does not compile: parameters are
// registered with ParameterGroup::add and saved by the group alone.
static_assert(!Miro::Reflectable<FloatParam>);
static_assert(!Miro::Reflectable<ChoiceParam>);
static_assert(Miro::Reflectable<double>);

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
//
// A duplicate id within one group asserts in add(), which a test cannot catch.
namespace
{
bool near(float a, float b, float tolerance = 1e-5f)
{
    return std::abs(a - b) < tolerance;
}

std::vector<std::string> idsOf(const ParameterList& list)
{
    auto ids = std::vector<std::string> {};

    for (const auto& entry: list)
        ids.push_back(entry.id);

    return ids;
}

std::vector<std::string> namesOf(const ParameterList& list)
{
    auto names = std::vector<std::string> {};

    for (const auto& entry: list)
        names.push_back(entry.displayName);

    return names;
}

struct OscParams : ParameterGroup
{
    explicit OscParams(std::string_view name)
        : ParameterGroup(name)
    {
        add(attack, wave);
    }

    TimeParam attack {"Attack", 0.f, 5.f, 0.01f};
    ChoiceParam wave {"Wave", {"Sine", "Saw"}, 0};
};

struct FilterParams : ParameterGroup
{
    FilterParams()
        : ParameterGroup("Filter")
    {
        add(cutoff);
    }

    HzParam cutoff {"Cutoff", 20.f, 20000.f, 1000.f};
};

struct SynthParams : ParameterGroup
{
    SynthParams() { add(gain, osc1, filter, mute, hand); }

    DecibelParam gain {"Gain", -60.f, 12.f, 0.f};
    OscParams osc1 {"Osc 1"};
    FilterParams filter;
    BoolParam mute {"Mute", false, {.automatable = false, .hostId = 42}};
    ChoiceParam hand {"Hand", {"Left", "Right"}, 0, {.sessionOnly = true}};
};

auto tOrder = test("Parameters/theListFollowsRegistrationOrder") = []
{
    auto params = SynthParams {};
    auto list = ParameterList {params};

    check(list.size() == 6);
    check(
        idsOf(list)
        == std::vector<std::string> {
            "Gain", "Osc 1/Attack", "Osc 1/Wave", "Filter/Cutoff", "Mute", "Hand"});

    check(&list[0] == &params.gain);
    check(&list[2] == &params.osc1.wave);
    check(list.indexOf(&params.filter.cutoff) == 3);
    check(list.indexOf("Osc 1/Attack") == 1);
    check(list.indexOf("Attack") == -1);
};

auto tDisplayNames = test("Parameters/groupsPrefixDisplayNames") = []
{
    auto params = SynthParams {};
    auto list = ParameterList {params};

    check(params.name().empty());
    check(params.osc1.name() == "Osc 1");
    check(params.osc1.id() == "Osc 1");

    check(list.entry(0).displayName == "Gain");
    check(list.entry(1).displayName == "Osc 1 Attack");
    check(list.entry(2).displayName == "Osc 1 Wave");
    check(list.entry(3).displayName == "Filter Cutoff");
};

// A parameter renamed at run time, as a hosted plugin's is by the plugin.
struct RenamableParam : FloatParam
{
    using FloatParam::FloatParam;
    using FloatParam::setDisplayInfo;
};

struct RenamableGroup : ParameterGroup
{
    RenamableGroup() { add(level, filter); }

    struct Filter : ParameterGroup
    {
        Filter()
            : ParameterGroup("Filter")
        {
            add(cutoff);
        }

        RenamableParam cutoff {"Cutoff", 0.f, 1.f, 0.5f};
    };

    RenamableParam level {"Level", 0.f, 1.f, 0.5f};
    Filter filter;
};

auto tRename = test("Parameters/aRenameKeepsTheIdsAndTheGroupNames") = []
{
    auto params = RenamableGroup {};
    auto list = ParameterList {params};
    const auto hostIds = std::array {list.entry(0).hostId, list.entry(1).hostId};

    check(!list.refreshDisplayNames());

    params.level.setDisplayInfo("Volume", "Vol", "dB");
    params.filter.cutoff.setDisplayInfo("Frequency", "", "Hz");

    check(params.level.name() == "Volume");
    check(params.level.shortName() == "Vol");
    check(params.level.label() == "dB");
    check(params.level.id() == "Level");

    // The list still shows the old names until it is told.
    check(list.entry(0).displayName == "Level");
    check(list.refreshDisplayNames());
    check(!list.refreshDisplayNames());

    check(list.entry(0).displayName == "Volume");
    check(list.entry(1).displayName == "Filter Frequency");
    check(list.entry(0).id == "Level");
    check(list.entry(1).id == "Filter/Cutoff");
    check(list.entry(0).hostId == hostIds[0]);
    check(list.entry(1).hostId == hostIds[1]);
};

// A hosted plugin's parameters: ids the foreign plugin chose, all 32 bits.
struct ForeignParams : ParameterGroup
{
    ForeignParams() { add(low, high, top); }

    FloatParam low {"Low", 0.f, 1.f, 0.f, {.id = "7", .hostId = 7}};
    FloatParam high {
        "High", 0.f, 1.f, 0.f, {.id = "2147483649", .hostId = 0x80000001u}};
    FloatParam top {"Top", 0.f, 1.f, 0.f, hiddenTop};

    static constexpr auto hiddenTop = MakeASound::ParameterOptions {
        .id = "4294967294", .automatable = false, .hostId = 0xfffffffeu};
};

auto tForeign = test("Parameters/aHostedPluginsListCarriesItsOwnIds") = []
{
    auto params = ForeignParams {};

    // Under a plugin's own rules, the reserved range is reported.
    auto native = ParameterList {params};
    check(native.identityProblems().size() == 2);

    auto hosted = ParameterList {params, MakeASound::hostedPluginIdentity()};
    check(hosted.identityProblems().empty());

    check(hosted.entry(0).hostId == 7u);
    check(hosted.entry(1).hostId == 0x80000001u);
    check(hosted.entry(2).hostId == 0xfffffffeu);
    check(hosted.indexOfHostId(0x80000001u) == 1);
    check(hosted.indexOfHostId(0xfffffffeu) == 2);
    check(hosted.entry(2).id == "4294967294");

    // Every one listed, a non-automatable one too, as the plugin listed them.
    check(!hosted.isHostExposed(2));
    check(hosted.isRegistered(2));

    // Set again, as a plugin's setParameters does, nothing is re-derived.
    hosted.setIdentity(MakeASound::hostedPluginIdentity());
    check(hosted.entry(1).hostId == 0x80000001u);
};

struct TwoOscParams : ParameterGroup
{
    TwoOscParams() { add(osc1, osc2); }

    OscParams osc1 {"Osc 1"};
    OscParams osc2 {"Osc 2"};
};

auto tInstances = test("Parameters/twoInstancesOfAGroupTypeKeepTheirOwnNames") = []
{
    auto params = TwoOscParams {};
    auto list = ParameterList {params};

    check(idsOf(list)
          == std::vector<std::string> {
              "Osc 1/Attack", "Osc 1/Wave", "Osc 2/Attack", "Osc 2/Wave"});
    check(list.entry(2).displayName == "Osc 2 Attack");
    check(&list[2] == &params.osc2.attack);
    check(list.entry(0).hostId != list.entry(2).hostId);
};

// A group that names nothing itself, mounted by its parent under a name and an
// id of the parent's choosing, once per member and once per array element.
struct LfoParams : ParameterGroup
{
    LfoParams() { add(rate); }

    FloatParam rate {"Rate", 0.f, 1.f, 0.5f};
};

struct MountedParams : ParameterGroup
{
    MountedParams()
    {
        addChild("Osc 1", osc1, {.id = "Osc1"});
        addChild("Osc 2", osc2, {.id = "Osc2", .automatable = false});

        for (auto i = 0; i < static_cast<int>(lfos.size()); ++i)
        {
            auto n = std::to_string(i + 1);
            addChild("LFO " + n, lfos[i], {.id = "Lfo" + n});
        }

        addChild("Tone", tone);
    }

    OscParams osc1 {"Unused"};
    OscParams osc2 {"Unused"};
    std::array<LfoParams, 2> lfos;
    LfoParams tone;
};

auto tMounted = test("Parameters/aParentNamesTheChildrenItMounts") = []
{
    auto params = MountedParams {};
    auto list = ParameterList {params};

    check(idsOf(list)
          == std::vector<std::string> {"Osc1/Attack",
                                       "Osc1/Wave",
                                       "Osc2/Attack",
                                       "Osc2/Wave",
                                       "Lfo1/Rate",
                                       "Lfo2/Rate",
                                       "Tone/Rate"});
    check(namesOf(list)
          == std::vector<std::string> {"Osc 1 Attack",
                                       "Osc 1 Wave",
                                       "Osc 2 Attack",
                                       "Osc 2 Wave",
                                       "LFO 1 Rate",
                                       "LFO 2 Rate",
                                       "Tone Rate"});

    check(params.osc1.name() == "Osc 1" && params.osc1.id() == "Osc1");
    check(params.tone.id() == "Tone");
    check(&list[5] == &params.lfos[1].rate);

    check(list.entry(1).automatable);
    check(!list.entry(2).automatable && !list.entry(3).automatable);

    const auto& groups = list.entry(4).groups;
    check(groups.size() == 1);
    check(groups[0].id == "Lfo1" && groups[0].name == "LFO 1");
};

auto tFlags = test("Parameters/entriesCarryTheParameterFlags") = []
{
    auto params = SynthParams {};
    auto list = ParameterList {params};

    check(list.isHostExposed(0));
    check(!list.isHostExposed(4));
    check(!list.isHostExposed(-1));
    check(!list.isHostExposed(list.size()));

    check(list.entry(5).sessionOnly);
    check(!list.entry(0).sessionOnly);
};

struct HiddenParams : ParameterGroup
{
    explicit HiddenParams(std::string_view name)
        : ParameterGroup(name, {.automatable = false})
    {
        add(level);
    }

    FloatParam level {"Level"};
};

struct MixedExposureParams : ParameterGroup
{
    MixedExposureParams() { add(shown, hidden); }

    FloatParam shown {"Shown"};
    HiddenParams hidden {"Hidden"};
};

auto tGroupFlags = test("Parameters/aNonAutomatableGroupHidesItsParameters") = []
{
    auto params = MixedExposureParams {};
    auto list = ParameterList {params};

    check(list.isHostExposed(0));
    check(!list.isHostExposed(1));
    check(params.hidden.level.isAutomatable());
};

struct BypassParams : ParameterGroup
{
    BypassParams() { add(gain, hiddenBypass, bypass); }

    FloatParam gain {"Gain"};
    BoolParam hiddenBypass {"Hidden", false, {.automatable = false, .bypass = true}};
    BoolParam bypass {"Bypass", false, {.bypass = true}};
};

struct HiddenBypassParams : ParameterGroup
{
    HiddenBypassParams() { add(gain, bypass); }

    FloatParam gain {"Gain"};
    BoolParam bypass {"Bypass", false, {.automatable = false, .bypass = true}};
};

auto tBypass = test("Parameters/bypassIndexIsTheExposedBypassEntry") = []
{
    auto params = BypassParams {};
    auto list = ParameterList {params};

    check(list.bypassIndex() == 2);
    check(&list[list.bypassIndex()] == &params.bypass);
    check(list.entry(2).bypass);
    check(!list.entry(1).bypass && !list.entry(0).bypass);
};

auto tNoBypass = test("Parameters/bypassIndexIsMinusOneWithNone") = []
{
    auto params = SynthParams {};
    check(ParameterList {params}.bypassIndex() == -1);
    check(ParameterList {}.bypassIndex() == -1);
};

auto tHiddenBypass = test("Parameters/aNonAutomatableBypassIsNoBypass") = []
{
    auto params = HiddenBypassParams {};
    auto list = ParameterList {params};

    check(list.bypassIndex() == -1);
    check(!list.entry(1).bypass);
};

auto tFnv = test("Parameters/hostIdsAreFnv1aOfTheId") = []
{
    check(hostIdFor("a") == 0x640c292cu);
    check(hostIdFor("Gain") == 0x1b80731eu);
    check(hostIdFor("Osc 1/Attack") == 0x491d762cu);

    auto params = SynthParams {};
    auto list = ParameterList {params};

    check(list.entry(0).hostId == 0x1b80731eu);
    check(list.entry(1).hostId == 0x491d762cu);
    check(list.entry(4).hostId == 42u);
};

auto tHostIdBits = test("Parameters/hostIdsLeaveTheTopBitClear") = []
{
    auto params = SynthParams {};
    auto list = ParameterList {params};

    for (const auto& entry: list)
        check((entry.hostId & 0x80000000u) == 0u);
};

struct BandParams : ParameterGroup
{
    explicit BandParams(std::string_view name)
        : ParameterGroup(name)
    {
        add(gain, q);
    }

    FloatParam gain {"Gain", -24.f, 24.f, 0.f};
    FloatParam q {"Q", 0.1f, 10.f, 1.f};
};

struct EqParams : ParameterGroup
{
    EqParams()
    {
        shelves[0] = EA::makeOwned<BandParams>("Low");
        shelves[1] = EA::makeOwned<BandParams>("High");

        add(output, bands, shelves, sends);
    }

    FloatParam output {"Output", -24.f, 24.f, 0.f};
    std::array<BandParams, 2> bands {BandParams {"Band 1"}, BandParams {"Band 2"}};
    EA::Array<EA::OwningPointer<BandParams>, 2> shelves;
    std::array<FloatParam, 2> sends {FloatParam {"Send A"}, FloatParam {"Send B"}};
};

auto tRanges = test("Parameters/rangesRegisterEveryElementUnderItsOwnName") = []
{
    auto params = EqParams {};
    auto list = ParameterList {params};

    check(idsOf(list)
          == std::vector<std::string> {"Output",
                                       "Band 1/Gain",
                                       "Band 1/Q",
                                       "Band 2/Gain",
                                       "Band 2/Q",
                                       "Low/Gain",
                                       "Low/Q",
                                       "High/Gain",
                                       "High/Q",
                                       "Send A",
                                       "Send B"});

    check(namesOf(list)[3] == "Band 2 Gain");
    check(namesOf(list)[7] == "High Gain");

    check(&list[3] == &params.bands[1].gain);
    check(&list[7] == &params.shelves[1]->gain);
    check(&list[10] == &params.sends[1]);
};

struct VoiceParams : ParameterGroup
{
    VoiceParams()
    {
        voices.add(EA::makeOwned<BandParams>("Lead"));
        voices.add(EA::makeOwned<BandParams>("Bass"));
        add(voices);
    }

    EA::Vector<EA::OwningPointer<BandParams>> voices;
};

auto tVectors = test("Parameters/vectorsOfGroupsRegisterInOrder") = []
{
    auto params = VoiceParams {};
    auto list = ParameterList {params};

    check(list.size() == 4);
    check(list.entry(0).id == "Lead/Gain");
    check(list.entry(3).id == "Bass/Q");
    check(list.entry(2).displayName == "Bass Gain");
    check(&list[2] == &params.voices[1]->gain);
};

struct FlatParams : ParameterGroup
{
    FlatParams() { add(gain, inner); }

    struct Inner : ParameterGroup
    {
        Inner() { add(pan); }

        FloatParam pan {"Pan"};
    };

    FloatParam gain {"Gain"};
    Inner inner;
};

auto tNameless = test("Parameters/aNamelessGroupAddsNoSegment") = []
{
    auto params = FlatParams {};
    auto list = ParameterList {params};

    check(idsOf(list) == std::vector<std::string> {"Gain", "Pan"});
    check(namesOf(list) == std::vector<std::string> {"Gain", "Pan"});
};

// One release, then the next with every display name changed and ids pinned.
struct PinnedFilter : ParameterGroup
{
    PinnedFilter(std::string_view groupName, std::string_view cutoffName)
        : ParameterGroup(groupName, {.id = "filter"})
        , cutoff(cutoffName, 20.f, 20000.f, 1000.f, {.id = "cutoff"})
    {
        add(cutoff);
    }

    HzParam cutoff;
};

struct PinnedParams : ParameterGroup
{
    PinnedParams(std::string_view groupName, std::string_view cutoffName)
        : filter(groupName, cutoffName)
    {
        add(filter);
    }

    PinnedFilter filter;
};

auto tPinnedIds = test("Parameters/anExplicitIdOutlivesARename") = []
{
    auto before = PinnedParams {"Filter", "Cutoff"};
    auto after = PinnedParams {"Tone", "Frequency"};
    auto beforeList = ParameterList {before};
    auto afterList = ParameterList {after};

    check(beforeList.entry(0).id == "filter/cutoff");
    check(afterList.entry(0).id == "filter/cutoff");
    check(afterList.entry(0).hostId == beforeList.entry(0).hostId);
    check(beforeList.entry(0).displayName == "Filter Cutoff");
    check(afterList.entry(0).displayName == "Tone Frequency");
};

struct NestedParams : ParameterGroup
{
    NestedParams() { add(voice, flat); }

    struct Voice : ParameterGroup
    {
        Voice()
            : ParameterGroup("Voice")
        {
            add(filter);
        }

        PinnedFilter filter {"Tone", "Frequency"};
    };

    Voice voice;
    FlatParams flat;
};

auto tEnclosing = test("Parameters/entriesNameTheirEnclosingGroups") = []
{
    auto params = NestedParams {};
    auto list = ParameterList {params};

    check(list.size() == 3);
    check(list.entry(0).id == "Voice/filter/cutoff");

    const auto& groups = list.entry(0).groups;
    check(groups.size() == 2);
    check(groups[0].id == "Voice" && groups[0].name == "Voice");
    check(groups[1].id == "Voice/filter" && groups[1].name == "Tone");

    // A nameless group encloses nothing a host would show.
    check(list.entry(1).groups.empty());
    check(list.entry(2).groups.empty());
};

auto tHostIdLookup = test("Parameters/indexOfHostIdFindsEveryEntry") = []
{
    auto params = SynthParams {};
    auto list = ParameterList {params};

    for (auto i = 0; i < list.size(); ++i)
        check(list.indexOfHostId(list.entry(i).hostId) == i);

    check(list.indexOfHostId(7u) == -1);
    check(ParameterList {}.indexOfHostId(42u) == -1);
};

auto tFloat = test("Parameters/floatRoundTripsAndClamps") = []
{
    auto param = FloatParam {"Amount", -1.f, 3.f, 1.f};

    check(near(param.getNormalized(), 0.5f));
    check(near(param.toPlain(param.toNormalized(2.2f)), 2.2f));

    param.setNormalized(0.75f);
    check(near(param.get(), 2.f));

    param.setValue(10.f);
    check(near(param.getValue(), 3.f));

    param.setValue(NAN);
    check(near(param.getValue(), 3.f));

    param.setNormalized(-1.f);
    check(near(param.getValue(), -1.f));

    check(param.valueToText(1.5f) == "1.5");
    check(near(param.textToValue(" 2.25 "), 2.25f));
    check(near(param.textToValue("nonsense"), 1.f));

    auto withLabel = FloatParam {"Width", 0.f, 2.f, 1.f, {.label = "x"}};
    check(withLabel.valueToText(0.5f) == "0.5 x");
    check(near(withLabel.textToValue("1.25 x"), 1.25f));
};

auto tChoice = test("Parameters/choiceRoundTripsByIndexAndName") = []
{
    auto param = ChoiceParam {"Mode", {"Low", "Band", "High"}, 1};

    check(param.numSteps() == 2);
    check(near(param.getNormalized(), 0.5f));

    param.setNormalized(0.8f);
    check(param.getIndex() == 2);

    param.setNormalized(0.2f);
    check(param.getIndex() == 0);

    check(near(param.toPlain(0.4f), 1.f));
    check(param.valueToText(2.f) == "High");
    check(near(param.textToValue(" band "), 1.f));
    check(near(param.textToValue("Notch"), 1.f));

    param.setValue(7.f);
    check(param.getIndex() == 2);

    check(param.fromStateText("low"));
    check(param.getIndex() == 0);
    check(!param.fromStateText("Notch"));
    check(param.getIndex() == 0);
};

auto tBool = test("Parameters/boolRoundTrips") = []
{
    auto param = BoolParam {"Retrigger", false, "Free", "Retrig"};

    check(param.numSteps() == 1);
    check(!param.isOn());

    param.setNormalized(0.9f);
    check(param.isOn());
    check(near(param.getNormalized(), 1.f));

    check(param.valueToText(0.f) == "Free");
    check(near(param.textToValue("retrig"), 1.f));
    check(near(param.textToValue("free"), 0.f));
    check(near(param.textToValue("whatever"), 0.f));

    check(param.fromStateText("false"));
    check(!param.isOn());
};

auto tDecibel = test("Parameters/decibelReadsInDbAndGain") = []
{
    auto param = DecibelParam {"Gain", -60.f, 12.f, 0.f};

    check(near(param.gain(), 1.f));
    check(param.valueToText(-6.f) == "-6.0 dB");
    check(param.valueToText(3.f) == "+3.0 dB");
    check(near(param.textToValue("-12.5 dB"), -12.5f));

    param.setValue(-20.f);
    check(near(param.gain(), 0.1f));
    check(near(param.toPlain(param.toNormalized(-20.f)), -20.f, 1e-4f));
};

auto tHz = test("Parameters/hzReadsHzAndKhz") = []
{
    auto param = HzParam {"Cutoff", 20.f, 20000.f, 1000.f};

    check(param.valueToText(440.f) == "440.00 Hz");
    check(near(param.textToValue("440 Hz"), 440.f));
    check(near(param.textToValue("2.5 kHz"), 2500.f));
    check(near(param.toPlain(param.toNormalized(440.f)), 440.f, 1e-2f));
};

// A Tamber Plug HzParam was linear; a port keeps that through the range.
auto tLinearHz = test("Parameters/aPlainRangeKeepsAnHzParamLinear") = []
{
    auto param = HzParam {"Rate", FloatRange {0.1f, 20.1f}, 10.1f};

    check(param.curve() == Parameter::Curve::Linear);
    check(param.numSteps() == 0);
    check(near(param.getNormalized(), 0.5f));
    check(near(param.toNormalized(5.1f), 0.25f));
    check(near(param.toPlain(0.75f), 15.1f, 1e-4f));
    check(param.valueToText(5.1f) == "5.10 Hz");

    auto skewed = HzParam {"Rate", 0.1f, 20.1f, 10.1f};
    check(skewed.curve() == Parameter::Curve::Other);
};

auto tTime = test("Parameters/timeReadsMsAndSeconds") = []
{
    auto param = TimeParam {"Attack", 0.f, 5.f, 0.01f};

    check(param.valueToText(0.25f) == "250 ms");
    check(param.valueToText(1.5f) == "1.50 s");
    check(near(param.textToValue("250 ms"), 0.25f));
    check(near(param.textToValue("2 s"), 2.f));
    check(near(param.textToValue("3"), 3.f));
    check(near(param.textToValue("300"), 0.3f));
    check(near(param.toPlain(param.toNormalized(0.25f)), 0.25f));
};

auto tPercent = test("Parameters/percentReadsWholePercentages") = []
{
    auto param = PercentParam {"Mix", 0.5f};

    check(param.valueToText(0.25f) == "25 %");
    check(near(param.textToValue("75 %"), 0.75f));
    check(near(param.textToValue("??"), 0.5f));
    check(near(param.getNormalized(), 0.5f));
};

// The expected figures are JUCE 8.0.12's NormalisableRange<float> answers, printed
// from JUCE itself; a bit-for-bit sweep of ~2M inputs against JUCE matched too,
// but the transcendental ones are compared within a tolerance here because
// another platform's libm may round powf/expf/logf in their last bit.
auto tSkew = test("Parameters/aSkewedRangeMapsAsJuceDoes") = []
{
    auto range = FloatRange {20.f, 20000.f, 0.f, 0.3f};
    check(near(range.toPlain(0.25f), 216.665802f, 1e-3f));
    check(near(range.toNormalized(1000.f), 0.404752135f, 1e-6f));
    check(range.toPlain(0.f) == 20.f);
    check(range.toPlain(1.f) == 20000.f);
    check(range.toNormalized(20.f) == 0.f);
    check(range.toNormalized(20000.f) == 1.f);

    auto symmetric = FloatRange {-1.f, 1.f, 0.f, 0.5f, true};
    check(near(symmetric.toPlain(0.3f), -0.159999967f, 1e-6f));
    check(near(symmetric.toNormalized(0.2f), 0.723606825f, 1e-6f));
    check(near(symmetric.toNormalized(-0.2f), 0.276393205f, 1e-6f));
    check(symmetric.toPlain(0.5f) == 0.f);

    auto param = FloatParam {"Cutoff", range, 1000.f};
    check(param.curve() == Parameter::Curve::Other);
    check(param.numSteps() == 0);
    check(near(param.toPlain(param.toNormalized(1000.f)), 1000.f, 1e-2f));

    param.setNormalized(0.25f);
    check(near(param.get(), 216.665802f, 1e-3f));
};

auto tCentre = test("Parameters/withCentrePutsTheCentreMidTravel") = []
{
    auto range = FloatRange::withCentre(20.f, 20000.f, std::sqrt(20.f * 20000.f));
    check(near(range.skew, 0.19889383f, 1e-6f));
    check(!range.symmetricSkew);
    check(near(range.toPlain(0.5f), 632.455322f, 1e-2f));
    check(near(range.toNormalized(1000.f), 0.549002707f, 1e-5f));

    // HzParam is that range: the geometric mean at the middle.
    auto hz = HzParam {"Cutoff", 20.f, 20000.f, 1000.f};
    check(hz.range().skew == range.skew);
    check(near(hz.toPlain(0.5f), 632.455322f, 1e-2f));
    check(hz.curve() == Parameter::Curve::Other);

    // From 0 Hz no geometric mean exists: linear.
    auto fromZero = HzParam {"Shift", 0.f, 100.f, 0.f};
    check(fromZero.range().isLinear());
    check(near(fromZero.toPlain(0.5f), 50.f));
};

auto tInterval = test("Parameters/anIntervalSnapsAndSteps") = []
{
    auto range = FloatRange {0.f, 10.f, 0.25f};
    check(range.snap(3.13f) == 3.25f);
    check(range.snap(3.125f) == 3.25f);
    check(range.snap(-4.f) == 0.f);
    check(range.snap(11.f) == 10.f);
    check(range.numSteps() == 40);

    auto param = FloatParam {"Amount", range, 3.1f};
    check(param.defaultValue() == 3.f);
    check(param.numSteps() == 40);

    param.setValue(7.4f);
    check(param.get() == 7.5f);

    param.setNormalized(0.51f);
    check(param.get() == 5.f);
    check(param.toNormalized(5.1f) == 0.5f);

    // A skewed grid is not evenly spaced in 0..1: the host sees it continuous,
    // and the values still snap.
    auto skewed = FloatParam {"Skewed", {0.f, 10.f, 1.f, 0.5f}, 2.f};
    check(skewed.numSteps() == 0);
    skewed.setNormalized(0.3f);
    check(skewed.get() == std::round(skewed.get()));
};

auto tCurves = test("Parameters/aCurveIsNamedOnlyWhenExact") = []
{
    using Curve = Parameter::Curve;

    auto curveOf = [](FloatRange range)
    { return FloatParam {"P", range, range.min}.curve(); };

    check(curveOf({0.f, 1.f}) == Curve::Linear);
    check(curveOf({0.f, 1.f, 0.1f}) == Curve::Linear);
    check(curveOf({0.f, 1.f, 0.f, 0.5f}) == Curve::SquareRoot);
    check(curveOf({0.f, 1.f, 0.f, 2.f}) == Curve::Squared);
    check(curveOf({0.f, 1.f, 0.f, 3.f}) == Curve::Cubed);
    check(curveOf({0.f, 1.f, 0.f, 1.f / 3.f}) == Curve::CubeRoot);
    check(curveOf({0.f, 1.f, 0.f, 0.3f}) == Curve::Other);
    check(curveOf({-1.f, 1.f, 0.f, 0.5f, true}) == Curve::Other);
    check(curveOf({-1.f, 1.f, 0.f, 1.f, true}) == Curve::Linear);

    check(ChoiceParam {"C", {"A", "B"}}.curve() == Curve::Linear);
    check(BoolParam {"B"}.curve() == Curve::Linear);
};

auto tInt = test("Parameters/anIntStepsByOneAndRoundsAsJuceDoes") = []
{
    auto param = IntParam {"Voices", 1, 3, 1, {.label = "v"}};

    check(param.numSteps() == 2);
    check(param.minValue() == 1.f && param.maxValue() == 3.f);
    check(param.curve() == Parameter::Curve::Linear);

    // JUCE's own AudioParameterInt cases.
    for (auto [normalized, expected]: std::array<std::pair<float, int>, 6> {
             {{0.f, 1}, {0.2f, 1}, {0.3f, 2}, {0.7f, 2}, {0.8f, 3}, {1.f, 3}}})
    {
        param.setNormalized(normalized);
        check(param.get() == expected);
    }

    // Half to even, as juce::roundToInt: 0.25 of 1..3 is 1.5, which rounds to 2,
    // and 0.75 is 2.5, which rounds to 2 too.
    check(param.toPlain(0.25f) == 2.f);
    check(param.toPlain(0.75f) == 2.f);

    param.setValue(-7.f);
    check(param.get() == 1);
    param.setValue(2.6f);
    check(param.get() == 3);
    param.setValue(NAN);
    check(param.get() == 3);

    check(param.valueToText(2.f) == "2 v");
    check(param.textToValue("2.7") == 3.f);
    check(param.textToValue("none") == 1.f);
    check(param.toStateText() == "3");

    // -0.4 of an int range from -1 rounds to +0, never -0.
    auto signedParam = IntParam {"Offset", -1, 2, 0};
    check(!std::signbit(signedParam.toPlain(0.2f)));
};

auto tChoiceRounding = test("Parameters/aChoiceRoundsHalfToEvenAsJuceDoes") = []
{
    auto param = ChoiceParam {"Mode", {"A", "B", "C"}, 0};

    // 0.25 of 0..2 is 0.5: JUCE's roundToInt gives 0, 0.75 is 1.5 and gives 2.
    check(param.toPlain(0.25f) == 0.f);
    check(param.toPlain(0.75f) == 2.f);
    check(param.toNormalized(1.f) == 0.5f);
};

auto tVersionHint = test("Parameters/theVersionHintIsCarried") = []
{
    auto hinted = FloatParam {"Drive", 0.f, 1.f, 0.f, {.versionHint = 3}};
    auto unset = FloatParam {"Tone"};

    check(hinted.versionHint() == 3);
    check(unset.versionHint() == 0);
};
} // namespace
