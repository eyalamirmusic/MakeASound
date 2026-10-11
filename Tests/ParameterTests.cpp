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
using MakeASound::hostIdFor;
using MakeASound::HzParam;
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
} // namespace
