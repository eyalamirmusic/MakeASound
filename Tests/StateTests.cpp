// State<ParamsT>: the document a parameter group saves to and loads from, how tolerant loading is, the session-only tier, and the published
// snapshot a host reads off the message thread.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <string>
#include <vector>

using namespace nano;

using MakeASound::BoolParam;
using MakeASound::ChoiceParam;
using MakeASound::FloatParam;
using MakeASound::ParameterGroup;
using MakeASound::ParameterList;
using MakeASound::PercentParam;
using MakeASound::State;
using MakeASound::StateContext;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
bool near(float a, float b)
{ return std::abs(a - b) < 1e-6f; }

bool contains(const std::string& text, const std::string& part)
{ return text.find(part) != std::string::npos; }

struct ToneParams : ParameterGroup
{
    ToneParams() { add(gain, mode, bright, mix); }

    FloatParam gain {"Gain", 0.f, 1.f, 0.5f};
    ChoiceParam mode {"Mode", {"Low", "Band", "High"}, 1};
    BoolParam bright {"Bright", false};
    PercentParam mix {"Mix", 1.f};
};

// A later release with a parameter inserted mid-list and the choice list
// reshuffled: every original name survives, at a different index.
struct NewerToneParams : ParameterGroup
{
    NewerToneParams() { add(gain, drive, mode, bright, mix); }

    FloatParam gain {"Gain", 0.f, 1.f, 0.5f};
    FloatParam drive {"Drive", 0.f, 1.f, 0.2f};
    ChoiceParam mode {"Mode", {"Notch", "High", "Band", "Low"}, 0};
    BoolParam bright {"Bright", false};
    PercentParam mix {"Mix", 1.f};
};

struct OscParams : ParameterGroup
{
    explicit OscParams(std::string_view name)
        : ParameterGroup(name)
    { add(level); }

    FloatParam level {"Level", 0.f, 1.f, 0.8f};
};

struct SynthParams : ParameterGroup
{
    SynthParams() { add(gain, osc1, hand); }

    FloatParam gain {"Gain", 0.f, 1.f, 0.5f};
    OscParams osc1 {"Osc 1"};
    ChoiceParam hand {"Hand", {"Off", "Left", "Right"}, 1, {.sessionOnly = true}};
};

struct ToneState : State<ToneParams>
{
    void reflect(Miro::Reflector& ref) override
    {
        State::reflect(ref);
        ref["preset"](preset);
    }

    std::string preset = "Init";
};

auto tRoundTrip = test("State/roundTripsEveryParameterType") = []
{
    auto saved = ToneState {};
    saved.params.gain.setValue(0.73f);
    saved.params.mode.setValue(2.f);
    saved.params.bright.setOn(true);
    saved.params.mix.setValue(0.25f);
    saved.preset = "Bright lead";

    auto loaded = ToneState {};
    loaded.deserialize(saved.serialize());

    check(near(loaded.params.gain.get(), 0.73f));
    check(loaded.params.mode.getIndex() == 2);
    check(loaded.params.bright.isOn());
    check(near(loaded.params.mix.get(), 0.25f));
    check(loaded.preset == "Bright lead");
};

auto tDocumentShape = test("State/savesChoicesByNameAndBoolsAsBools") = []
{
    auto state = State<ToneParams> {};
    state.params.gain.setValue(0.73f);
    auto json = state.serialize();

    check(contains(json, R"("version":1)"));
    check(contains(json, R"("Gain":0.73)"));
    check(contains(json, R"("Mode":"Band")"));
    check(contains(json, R"("Bright":false)"));
};

auto tChoiceByIndex = test("State/loadsAChoiceSavedAsItsIndex") = []
{
    auto state = State<ToneParams> {};
    state.deserialize(R"({"params":{"Mode":2}})");

    check(state.params.mode.getIndex() == 2);
};

auto tUnknownChoice = test("State/anUnknownChoiceNameRestoresTheDefault") = []
{
    auto state = State<ToneParams> {};
    state.params.mode.setValue(0.f);
    state.deserialize(R"({"params":{"Mode":"Notch"}})");

    check(state.params.mode.getIndex() == 1);
};

auto tMissingKey = test("State/aMissingKeyRestoresItsDefault") = []
{
    auto state = State<SynthParams> {};
    state.params.gain.setValue(0.9f);
    state.params.osc1.level.setValue(0.1f);
    state.deserialize(R"({"params":{"Gain":0.25}})");

    check(near(state.params.gain.get(), 0.25f));
    check(near(state.params.osc1.level.get(), 0.8f));
};

auto tNoParams = test("State/aDocumentWithNoParamsMovesNothing") = []
{
    auto state = State<ToneParams> {};
    state.params.gain.setValue(0.9f);
    state.deserialize(R"({"version":3})");

    check(near(state.params.gain.get(), 0.9f));
    check(state.loadedVersion == 3);
    check(state.version == 1);
};

struct SecondSchemaState : State<ToneParams>
{
    SecondSchemaState() { version = 2; }
};

auto tVersion = test("State/loadingReadsTheVersionWithoutAdoptingIt") = []
{
    auto state = SecondSchemaState {};
    state.deserialize(R"({"version":1,"params":{"Gain":0.25}})");

    check(state.loadedVersion == 1);
    check(state.version == 2);
    check(contains(state.serialize(), R"("version":2)"));
};

auto tMalformed = test("State/malformedJsonMovesNothing") = []
{
    auto state = State<ToneParams> {};
    state.params.gain.setValue(0.4f);
    state.deserialize("not json at all");
    state.loadParams("{\"params\": [");

    check(near(state.params.gain.get(), 0.4f));
};

auto tNested = test("State/nestedGroupsNestInsideParams") = []
{
    auto saved = State<SynthParams> {};
    saved.params.osc1.level.setValue(0.3f);
    auto json = saved.serialize();

    check(contains(json, R"("Osc 1":{"Level":0.3})"));

    auto loaded = State<SynthParams> {};
    loaded.deserialize(json);
    check(near(loaded.params.osc1.level.get(), 0.3f));
};

auto tSessionOnly = test("State/aSessionOnlyParameterMovesOnlyInASession") = []
{
    auto saved = State<SynthParams> {};
    saved.params.hand.setValue(2.f);

    auto preset = saved.serialize(StateContext::Preset);
    auto session = saved.serialize(StateContext::Session);

    check(!contains(preset, "Hand"));
    check(contains(session, R"("Hand":"Right")"));

    auto loaded = State<SynthParams> {};
    loaded.params.hand.setValue(0.f);

    loaded.deserialize(session, StateContext::Preset);
    check(loaded.params.hand.getIndex() == 0);

    loaded.deserialize(preset, StateContext::Session);
    check(loaded.params.hand.getIndex() == 1);

    loaded.deserialize(session, StateContext::Session);
    check(loaded.params.hand.getIndex() == 2);
};

auto tInsertedParameter =
    test("State/aParameterInsertedMidListKeepsOlderOnesWhole") = []
{
    auto older = State<ToneParams> {};
    older.params.gain.setValue(0.7f);
    older.params.mode.setValue(0.f);
    older.params.mix.setValue(0.3f);

    auto newer = State<NewerToneParams> {};
    newer.params.drive.setValue(0.9f);
    newer.deserialize(older.serialize());

    check(near(newer.params.gain.get(), 0.7f));
    check(near(newer.params.drive.get(), 0.2f));
    check(newer.params.mode.choiceName(newer.params.mode.getIndex()) == "Low");
    check(near(newer.params.mix.get(), 0.3f));

    auto olderParams = ToneParams {};
    auto newerParams = NewerToneParams {};
    auto olderList = ParameterList {olderParams};
    auto newerList = ParameterList {newerParams};

    for (const auto& entry: olderList)
    {
        auto index = newerList.indexOf(entry.id);
        check(index >= 0);
        check(newerList.entry(index).hostId == entry.hostId);
    }
};

auto tLoadParams = test("State/loadParamsMovesParametersAndNothingElse") = []
{
    auto state = ToneState {};
    state.loadParams(R"({"params":{"Gain":0.2},"preset":"Other"})");

    check(near(state.params.gain.get(), 0.2f));
    check(state.preset == "Init");
    check(state.params.mode.getIndex() == 1);

    state.loadParams(R"({"preset":"Other"})");
    check(near(state.params.gain.get(), 0.2f));
};

auto tExceptParams = test("State/deserializeExceptParamsLeavesParameters") = []
{
    auto state = ToneState {};
    state.params.gain.setValue(0.9f);
    state.deserializeExceptParams(R"({"params":{"Gain":0.2},"preset":"Other"})");

    check(near(state.params.gain.get(), 0.9f));
    check(state.preset == "Other");

    state.deserialize(R"({"params":{"Gain":0.2}})");
    check(near(state.params.gain.get(), 0.2f));
};

auto tWrongType = test("State/aWrongTypedValueRestoresTheDefault") = []
{
    auto state = State<ToneParams> {};
    state.params.gain.setValue(0.9f);
    state.params.bright.setOn(true);
    state.deserialize(R"({"params":{"Gain":[0.2],"Bright":{"on":true}}})");

    check(near(state.params.gain.get(), 0.5f));
    check(!state.params.bright.isOn());
};

auto tMissingGroup = test("State/aMissingGroupRestoresItsDefaults") = []
{
    auto state = State<SynthParams> {};
    state.params.osc1.level.setValue(0.1f);
    state.deserialize(R"({"params":{"Osc 1":3}})");

    check(near(state.params.osc1.level.get(), 0.8f));
};

struct VoiceParams : ParameterGroup
{
    explicit VoiceParams(std::string_view name)
        : ParameterGroup(name)
    { add(level); }

    FloatParam level {"Level", 0.f, 1.f, 0.8f};
};

struct VoicesParams : ParameterGroup
{
    VoicesParams()
    {
        voices.push_back(EA::makeOwned<VoiceParams>("Lead"));
        voices.push_back(EA::makeOwned<VoiceParams>("Bass"));
        add(voices);
    }

    std::vector<EA::OwningPointer<VoiceParams>> voices;
};

auto tVectorSurvivesLoad = test("State/aVectorOfGroupsSurvivesALoad") = []
{
    auto saved = State<VoicesParams> {};
    saved.params.voices[1]->level.setValue(0.3f);
    auto json = saved.serialize();

    check(contains(json, R"("Bass":{"Level":0.3})"));

    auto loaded = State<VoicesParams> {};
    auto list = ParameterList {loaded.params};
    auto* bass = loaded.params.voices[1].get();

    loaded.deserialize(json);
    loaded.loadParams(json);

    check(loaded.params.voices.size() == 2);
    check(loaded.params.voices[1].get() == bass);
    check(&list[1] == &bass->level);
    check(near(list[1].getValue(), 0.3f));
};

struct RenamedFilter : ParameterGroup
{
    RenamedFilter(std::string_view groupName, std::string_view cutoffName)
        : ParameterGroup(groupName, {.id = "filter"})
        , cutoff(cutoffName, 0.f, 1.f, 0.5f, {.id = "cutoff"})
    { add(cutoff); }

    FloatParam cutoff;
};

struct OldFilterParams : ParameterGroup
{
    OldFilterParams() { add(filter); }

    RenamedFilter filter {"Filter", "Cutoff"};
};

struct NewFilterParams : ParameterGroup
{
    NewFilterParams() { add(filter); }

    RenamedFilter filter {"Tone", "Frequency"};
};

auto tPinnedKeys = test("State/anExplicitIdKeepsTheKeyAcrossARename") = []
{
    auto older = State<OldFilterParams> {};
    older.params.filter.cutoff.setValue(0.25f);
    auto json = older.serialize();

    check(contains(json, R"("filter":{"cutoff":0.25})"));

    auto newer = State<NewFilterParams> {};
    newer.deserialize(json);

    check(near(newer.params.filter.cutoff.get(), 0.25f));
    check(contains(newer.serialize(), R"("filter":{"cutoff":0.25})"));
};

// Reads differently every time, like a parameter under automation.
struct MovingParam : MakeASound::Parameter
{
    MovingParam()
        : Parameter("Moving", {})
    {
    }

    float minValue() const noexcept override { return 0.f; }
    float maxValue() const noexcept override { return 1000.f; }
    float defaultValue() const noexcept override { return 0.f; }
    void setValue(float) noexcept override {}
    float getValue() const noexcept override { return static_cast<float>(++reads); }

    mutable int reads = 0;
};

struct MovingParams : ParameterGroup
{
    MovingParams() { add(moving); }

    MovingParam moving;
};

auto tMovingParams =
    test("State/parametersMovingBetweenReadsKeepTheSnapshotCurrent") = []
{
    auto state = State<MovingParams> {};
    state.publish();

    check(state.isPublishedDocumentCurrent());
};

auto tPresetSnapshot = test("State/thePresetSnapshotLeavesOutSessionParameters") = []
{
    auto state = State<SynthParams> {};
    state.params.hand.setValue(2.f);
    state.publish();
    state.params.gain.setValue(0.125f);

    auto preset = state.snapshotDocument(StateContext::Preset);
    auto session = state.snapshotDocument(StateContext::Session);

    check(!contains(preset, "Hand"));
    check(contains(preset, R"("Gain":0.125)"));
    check(contains(session, R"("Hand":"Right")"));
    check(contains(session, R"("Gain":0.125)"));
};

auto tPublish = test("State/publishedSnapshotReadsParametersLive") = []
{
    auto state = ToneState {};
    check(!state.isPublishedDocumentCurrent());

    state.publish();
    check(state.isPublishedDocumentCurrent());

    state.params.gain.setValue(0.1f);
    check(state.isPublishedDocumentCurrent());
    check(contains(state.snapshotDocument(), R"("Gain":0.1)"));
    check(!contains(state.publishedDocument(), R"("Gain":0.1)"));

    state.preset = "Changed";
    check(!state.isPublishedDocumentCurrent());

    state.publish();
    check(state.isPublishedDocumentCurrent());
    check(contains(state.snapshotDocument(), R"("preset":"Changed")"));
};

auto tMarkChanged = test("State/deserializeBroadcastsAChange") = []
{
    auto state = State<ToneParams> {};
    auto changes = 0;
    auto listener = EA::Listener {state.stateChanged(),
                                  [&] { ++changes; },
                                  EA::Listener::Modes::TriggerOnEvent};

    state.deserialize(R"({"params":{}})");
    check(changes == 1);

    state.markChanged();
    check(changes == 2);
};
} // namespace
