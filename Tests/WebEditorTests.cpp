// The web editor's page API over a plugin, through the calls a page makes and
// through a Miro bridge as the WebView binds it. No WebView is constructed, so
// this runs without a display or a web runtime.

#include "TestPlugins.h"

#include <MakeASound/Plugin/Web/ParameterApi.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <string>
#include <vector>

using namespace nano;
using namespace TestPlugins;
using namespace MakeASound::Web;

namespace
{
struct Gesture
{
    char kind;
    int index;
    float normalized = 0.f;
};

struct GestureRecorder final : HostEditListener
{
    void beginParameterEdit(int index) noexcept override
    {
        gestures.push_back({'b', index});
    }

    void performParameterEdit(int index, float normalized) noexcept override
    {
        gestures.push_back({'p', index, normalized});
    }

    void endParameterEdit(int index) noexcept override
    {
        gestures.push_back({'e', index});
    }

    std::vector<Gesture> gestures;
};

bool near(double a, double b)
{
    return std::abs(a - b) < 1e-4;
}

bool isGesture(const std::vector<Gesture>& gestures, int index, float normalized)
{
    return gestures.size() == 3 && gestures[0].kind == 'b'
           && gestures[0].index == index && gestures[1].kind == 'p'
           && gestures[1].index == index && near(gestures[1].normalized, normalized)
           && gestures[2].kind == 'e' && gestures[2].index == index;
}

// Every values event the API publishes, in order.
struct ValuesLog
{
    explicit ValuesLog(ParameterApi& apiToUse)
        : api(apiToUse)
    {
    }

    ParameterApi& api;
    std::vector<Vector<WebParameterValue>> events;
    EA::Listener listener {api.values.broadcaster(),
                           [this]
                           { events.push_back(api.values.snapshot().changed); },
                           EA::Listener::Modes::TriggerOnEvent};
};

auto tList = test("Web/getParametersListsEveryEntry") = []
{
    auto plugin = GainPlugin {};
    auto api = ParameterApi(plugin);
    auto list = api.getParameters().parameters;

    check(list.size() == plugin.parameters().size());

    for (auto i = 0; i < list.size(); ++i)
    {
        auto& entry = plugin.parameters().entry(i);
        check(list[i].id == entry.id);
        check(list[i].name == entry.displayName);
        check(list[i].steps == entry.param->numSteps());
        check(near(list[i].normalized, entry.param->getNormalized()));
        check(list[i].text == entry.param->valueToText(entry.param->getValue()));
    }

    auto cutoff = plugin.parameters().indexOf("Filter/Cutoff");
    check(list[cutoff].name == "Filter Cutoff");
    check(list[cutoff].key == "Cutoff");
    check(list[cutoff].groups.size() == 1 && list[cutoff].groups[0] == "Filter");

    auto gain = plugin.parameters().indexOf("Gain");
    check(list[gain].key == "Gain");
    check(list[gain].groups.empty());

    auto mode = plugin.parameters().indexOf("Mode");
    check(list[mode].steps == 2);
    check(list[mode].choices.size() == 3);
    check(list[mode].choices[1] == "Warm");

    auto bypass = plugin.parameters().indexOf("Bypass");
    check(list[bypass].steps == 1);
    check(list[bypass].choices.size() == 2);
    check(list[bypass].choices[0] == "Off");
    check(list[bypass].choices[1] == "On");

    for (auto name: {"Gain", "Filter/Cutoff", "Width"})
        check(list[plugin.parameters().indexOf(name)].choices.empty());
};

struct LabelledParams : ParameterGroup
{
    LabelledParams() { add(power, sync); }

    BoolParam power {"Power", true, "Bypassed", "Active"};
    BoolParam sync {"Sync", false, "Free", "Retrig"};
};

struct LabelledPlugin : StatePlugin<State<LabelledParams>>
{
    std::string_view name() const override { return "Labelled"; }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}
};

// A bool's choices are its own labels, off first, whatever its value or default.
auto tBoolLabels = test("Web/aBoolListsItsOwnLabelsOffThenOn") = []
{
    auto plugin = LabelledPlugin {};
    auto api = ParameterApi(plugin);
    auto list = api.getParameters().parameters;

    check(list.size() == 2);
    check(list[0].steps == 1);
    check(list[0].choices.size() == 2);
    check(list[0].choices[0] == "Bypassed");
    check(list[0].choices[1] == "Active");
    check(list[0].text == "Active");
    check(list[0].defaultText == "Active");

    check(list[1].choices.size() == 2);
    check(list[1].choices[0] == "Free");
    check(list[1].choices[1] == "Retrig");

    // The labels name steps, not the value: they hold after the value moves.
    api.edit({.id = "Power", .normalized = 0.0});
    auto after = api.getParameters().parameters;
    check(after[0].text == "Bypassed");
    check(after[0].choices.size() == 2);
    check(after[0].choices[0] == "Bypassed");
    check(after[0].choices[1] == "Active");
};

auto tGesture = test("Web/editWritesAndReportsBeginPerformEnd") = []
{
    auto plugin = GainPlugin {};
    auto recorder = GestureRecorder {};
    plugin.setHostEditListener(&recorder);

    auto api = ParameterApi(plugin);
    auto index = plugin.parameters().indexOf("Gain");

    api.beginEdit({.id = "Gain"});
    api.edit({.id = "Gain", .normalized = 0.25});
    api.endEdit({.id = "Gain"});

    check(near(plugin.params.gain.getNormalized(), 0.25));
    check(isGesture(recorder.gestures, index, 0.25f));

    plugin.setHostEditListener(nullptr);
};

auto tWhole = test("Web/anEditOutsideAGestureIsAWholeOne") = []
{
    auto plugin = GainPlugin {};
    auto recorder = GestureRecorder {};
    plugin.setHostEditListener(&recorder);

    auto api = ParameterApi(plugin);
    auto index = plugin.parameters().indexOf("Mode");

    api.edit({.id = "Mode", .normalized = 1.0});

    check(plugin.params.mode.getIndex() == 2);
    check(isGesture(recorder.gestures, index, 1.f));

    plugin.setHostEditListener(nullptr);
};

auto tOpen = test("Web/endAllEditsClosesWhatThePageLeftOpen") = []
{
    auto plugin = GainPlugin {};
    auto recorder = GestureRecorder {};
    plugin.setHostEditListener(&recorder);

    auto api = ParameterApi(plugin);
    api.beginEdit({.id = "Gain"});
    api.beginEdit({.id = "Gain"});
    api.edit({.id = "Gain", .normalized = 0.5});
    api.endAllEdits();
    api.endEdit({.id = "Gain"});

    check(isGesture(recorder.gestures, plugin.parameters().indexOf("Gain"), 0.5f));

    plugin.setHostEditListener(nullptr);
};

auto tNoListener = test("Web/editWorksWithNoListener") = []
{
    auto plugin = GainPlugin {};
    auto api = ParameterApi(plugin);

    api.beginEdit({.id = "Bypass"});
    api.edit({.id = "Bypass", .normalized = 1.0});
    api.endEdit({.id = "Bypass"});

    check(plugin.params.bypass.isOn());
};

auto tUnknown = test("Web/anUnknownIdChangesNothing") = []
{
    auto plugin = GainPlugin {};
    auto recorder = GestureRecorder {};
    plugin.setHostEditListener(&recorder);

    auto api = ParameterApi(plugin);
    auto before = api.getParameters().parameters;

    api.beginEdit({.id = "Nope"});
    api.edit({.id = "Nope", .normalized = 0.9});
    api.endEdit({.id = "Nope"});

    auto after = api.getParameters().parameters;

    for (auto i = 0; i < before.size(); ++i)
        check(before[i].normalized == after[i].normalized);

    check(recorder.gestures.empty());

    plugin.setHostEditListener(nullptr);
};

auto tPoll = test("Web/pollPublishesOnlyWhatMoved") = []
{
    auto plugin = GainPlugin {};
    auto api = ParameterApi(plugin);
    auto log = ValuesLog(api);

    api.poll();
    check(log.events.empty());

    api.edit({.id = "Gain", .normalized = 0.75});
    plugin.params.filter.cutoff.setValue(5000.f);
    api.poll();

    check(log.events.size() == 1);
    check(log.events[0].size() == 2);
    check(log.events[0][0].id == "Gain");
    check(near(log.events[0][0].normalized, 0.75));
    check(log.events[0][0].text
          == plugin.params.gain.valueToText(plugin.params.gain.getValue()));
    check(log.events[0][1].id == "Filter/Cutoff");

    api.poll();
    check(log.events.size() == 1);

    plugin.params.mode.setValue(1.f);
    api.poll();

    check(log.events.size() == 2);
    check(log.events[1].size() == 1);
    check(log.events[1][0].id == "Mode");
    check(log.events[1][0].text == "Warm");
};

auto tBridge = test("Web/theBridgeServesThePageApi") = []
{
    auto plugin = GainPlugin {};
    auto recorder = GestureRecorder {};
    plugin.setHostEditListener(&recorder);

    auto api = ParameterApi(plugin);
    auto bridge = Miro::Bridge {};
    bridge.use(api);

    auto list = bridge.dispatch("getParameters", {})["parameters"];
    check(list.isArray());
    check(list.asArray().size() == static_cast<size_t>(plugin.parameters().size()));
    check(list[0]["id"].asString() == "Gain");

    bridge.dispatch("beginEdit", Miro::toJSON(WebParameterId {.id = "Gain"}));
    bridge.dispatch(
        "edit", Miro::toJSON(WebParameterEdit {.id = "Gain", .normalized = 0.5}));
    bridge.dispatch("endEdit", Miro::toJSON(WebParameterId {.id = "Gain"}));

    check(near(plugin.params.gain.getNormalized(), 0.5));
    check(isGesture(recorder.gestures, 0, 0.5f));

    plugin.setHostEditListener(nullptr);
};

auto tDefault = test("Web/aDefaultConstructedApiReflects") = []
{
    auto api = ParameterApi {};
    auto bridge = Miro::Bridge {};
    bridge.use(api);

    check(bridge.dispatch("getParameters", {})["parameters"].asArray().empty());
    check(bridge.dispatch("getParameterValues", {})["items"].asArray().empty());

    bridge.dispatch("edit", Miro::toJSON(WebParameterEdit {.id = "Gain"}));
    bridge.dispatch("editText",
                    Miro::toJSON(WebParameterText {.id = "Gain", .text = "1"}));
    bridge.dispatch("resetParam", Miro::toJSON(WebParameterId {.id = "Gain"}));
    bridge.dispatch("resetAll", {});
    api.poll();
    api.endAllEdits();
};

struct MixGroup : ParameterGroup
{
    MixGroup()
        : ParameterGroup("Mix")
    {
        add(dryWet);
    }

    FloatParam dryWet {"Dry/Wet", 0.f, 1.f, 1.f};
};

struct MixParams : ParameterGroup
{
    MixParams() { add(dryWet, mix); }

    FloatParam dryWet {"Dry/Wet", 0.f, 1.f, 1.f};
    MixGroup mix;
};

struct MixPlugin : StatePlugin<State<MixParams>>
{
    std::string_view name() const override { return "Mix"; }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}
};

auto tSlash = test("Web/anIdHoldingASlashIsAddressedByItsPath") = []
{
    auto plugin = MixPlugin {};
    auto api = ParameterApi(plugin);
    auto list = api.getParameters().parameters;

    check(list.size() == 2);
    check(list[0].id == "Dry//Wet");
    check(list[0].key == "Dry/Wet");
    check(list[0].groups.empty());
    check(list[1].id == "Mix/Dry//Wet");
    check(list[1].key == "Dry/Wet");
    check(list[1].groups.size() == 1 && list[1].groups[0] == "Mix");

    api.edit({.id = "Mix/Dry//Wet", .normalized = 0.25});
    check(near(plugin.params.mix.dryWet.getValue(), 0.25));
    check(near(plugin.params.dryWet.getValue(), 1.0));

    // The unescaped spelling names nothing.
    api.edit({.id = "Mix/Dry/Wet", .normalized = 0.5});
    check(near(plugin.params.mix.dryWet.getValue(), 0.25));
};

// Every keyed parameterValues event the API publishes, in order.
struct SnapshotLog
{
    explicit SnapshotLog(ParameterApi& apiToUse)
        : api(apiToUse)
    {
    }

    ParameterApi& api;
    std::vector<Vector<WebParameterValue>> events;
    EA::Listener listener {
        api.parameterValues.broadcaster(),
        [this] { events.push_back(api.parameterValues.snapshot().items); },
        EA::Listener::Modes::TriggerOnEvent};
};

auto tDefaults = test("Web/theSchemaCarriesEachDefault") = []
{
    auto plugin = GainPlugin {};
    auto api = ParameterApi(plugin);

    plugin.params.gain.setValue(-20.f);
    plugin.params.mode.setValue(2.f);

    auto list = api.getParameters().parameters;

    for (auto i = 0; i < list.size(); ++i)
    {
        auto& param = *plugin.parameters().entry(i).param;
        check(near(list[i].defaultNormalized,
                   param.toNormalized(param.defaultValue())));
        check(list[i].defaultText == param.valueToText(param.defaultValue()));
    }

    auto gain = plugin.parameters().indexOf("Gain");
    check(list[gain].defaultText == plugin.params.gain.valueToText(0.f));
    check(!near(list[gain].normalized, list[gain].defaultNormalized));

    auto mode = plugin.parameters().indexOf("Mode");
    check(list[mode].defaultNormalized == 0.0);
    check(list[mode].defaultText == "Clean");
    check(list[mode].text == "Hot");
};

auto tText = test("Web/editTextReadsThroughTheParameterAsOneGesture") = []
{
    auto plugin = GainPlugin {};
    auto recorder = GestureRecorder {};
    plugin.setHostEditListener(&recorder);

    auto api = ParameterApi(plugin);
    auto mode = plugin.parameters().indexOf("Mode");

    api.editText({.id = "Mode", .text = "Warm"});

    check(plugin.params.mode.getIndex() == 1);
    check(isGesture(recorder.gestures, mode, plugin.params.mode.getNormalized()));

    recorder.gestures.clear();
    auto gain = plugin.parameters().indexOf("Gain");
    auto expected = plugin.params.gain.textToValue("-6 dB");

    api.editText({.id = "Gain", .text = "-6 dB"});

    check(near(plugin.params.gain.getValue(), expected));
    check(near(plugin.params.gain.getValue(), -6.0));
    check(isGesture(recorder.gestures, gain, plugin.params.gain.getNormalized()));

    plugin.setHostEditListener(nullptr);
};

auto tTextInside = test("Web/editTextInsideAGestureJoinsIt") = []
{
    auto plugin = GainPlugin {};
    auto recorder = GestureRecorder {};
    plugin.setHostEditListener(&recorder);

    auto api = ParameterApi(plugin);
    auto cutoff = plugin.parameters().indexOf("Filter/Cutoff");

    api.beginEdit({.id = "Filter/Cutoff"});
    api.editText({.id = "Filter/Cutoff", .text = "440"});
    api.endEdit({.id = "Filter/Cutoff"});

    check(near(plugin.params.filter.cutoff.getValue(), 440.0));
    check(isGesture(
        recorder.gestures, cutoff, plugin.params.filter.cutoff.getNormalized()));

    plugin.setHostEditListener(nullptr);
};

auto tTextJunk = test("Web/editTextOfNoValueRestoresTheDefault") = []
{
    auto plugin = GainPlugin {};
    auto api = ParameterApi(plugin);

    plugin.params.gain.setValue(-30.f);
    api.editText({.id = "Gain", .text = "loud"});

    check(near(plugin.params.gain.getValue(), 0.0));

    api.editText({.id = "Nope", .text = "1"});
};

auto tReset = test("Web/resetParamRestoresTheDefaultAsOneGesture") = []
{
    auto plugin = GainPlugin {};
    auto recorder = GestureRecorder {};
    plugin.setHostEditListener(&recorder);

    auto api = ParameterApi(plugin);
    auto cutoff = plugin.parameters().indexOf("Filter/Cutoff");
    plugin.params.filter.cutoff.setValue(5000.f);

    api.resetParam({.id = "Filter/Cutoff"});

    // Through the skewed normalized range, so to its float precision.
    check(near(plugin.params.filter.cutoff.getValue() / 1000.0, 1.0));
    check(isGesture(
        recorder.gestures, cutoff, plugin.params.filter.cutoff.getNormalized()));

    recorder.gestures.clear();
    api.resetParam({.id = "Nope"});
    check(recorder.gestures.empty());

    plugin.setHostEditListener(nullptr);
};

auto tResetAll = test("Web/resetAllIsOneGesturePerMovedEntryAndSparesBypass") = []
{
    auto plugin = GainPlugin {};
    auto recorder = GestureRecorder {};
    plugin.setHostEditListener(&recorder);

    auto api = ParameterApi(plugin);
    auto& list = plugin.parameters();

    plugin.params.gain.setValue(-12.f);
    plugin.params.mode.setValue(2.f);
    plugin.params.bypass.setValue(1.f);

    api.resetAll();

    check(near(plugin.params.gain.getValue(), 0.0));
    check(plugin.params.mode.getIndex() == 0);
    check(plugin.params.bypass.isOn());

    // Gain and Mode only, in list order, each bracketed on its own.
    check(recorder.gestures.size() == 6);

    if (recorder.gestures.size() == 6)
    {
        auto first = std::vector<Gesture>(recorder.gestures.begin(),
                                          recorder.gestures.begin() + 3);
        auto second = std::vector<Gesture>(recorder.gestures.begin() + 3,
                                           recorder.gestures.end());

        check(isGesture(
            first, list.indexOf("Gain"), plugin.params.gain.getNormalized()));
        check(isGesture(second, list.indexOf("Mode"), 0.f));
    }

    recorder.gestures.clear();
    api.resetAll();
    check(recorder.gestures.empty());

    plugin.setHostEditListener(nullptr);
};

auto tResetOpen = test("Web/resetAllJoinsAGestureThePageHoldsOpen") = []
{
    auto plugin = GainPlugin {};
    auto recorder = GestureRecorder {};
    plugin.setHostEditListener(&recorder);

    auto api = ParameterApi(plugin);
    auto gain = plugin.parameters().indexOf("Gain");

    api.beginEdit({.id = "Gain"});
    api.edit({.id = "Gain", .normalized = 0.1});
    recorder.gestures.clear();

    api.resetAll();
    check(recorder.gestures.size() == 1);
    check(recorder.gestures[0].kind == 'p' && recorder.gestures[0].index == gain);

    api.endEdit({.id = "Gain"});
    check(recorder.gestures.size() == 2 && recorder.gestures[1].kind == 'e');

    plugin.setHostEditListener(nullptr);
};

auto tKeyed = test("Web/parameterValuesPublishesEveryEntryWhenOneMoved") = []
{
    auto plugin = GainPlugin {};
    auto api = ParameterApi(plugin);
    auto log = SnapshotLog(api);
    auto& list = plugin.parameters();

    auto first = api.getParameterValues().items;
    check(first.size() == list.size());

    for (auto i = 0; i < first.size(); ++i)
        check(first[i].id == list.entry(i).id);

    api.poll();
    check(log.events.empty());

    api.edit({.id = "Gain", .normalized = 0.75});
    api.poll();

    check(log.events.size() == 1);
    check(log.events[0].size() == list.size());

    auto gain = list.indexOf("Gain");
    check(near(log.events[0][gain].normalized, 0.75));
    check(log.events[0][gain].text
          == plugin.params.gain.valueToText(plugin.params.gain.getValue()));

    auto mode = list.indexOf("Mode");
    check(log.events[0][mode].text == "Clean");

    api.poll();
    check(log.events.size() == 1);
};

auto tKeyedBridge = test("Web/theBridgeServesTextResetAndTheKeyedEvent") = []
{
    auto plugin = GainPlugin {};
    auto recorder = GestureRecorder {};
    plugin.setHostEditListener(&recorder);

    auto api = ParameterApi(plugin);
    auto bridge = Miro::Bridge {};
    bridge.use(api);

    auto emitted = std::vector<std::string> {};
    auto keyed = Miro::JSON {};
    auto listener = EA::Listener {bridge.onEmit,
                                  [&]
                                  {
                                      auto name =
                                          std::string {bridge.currentEvent()};
                                      emitted.push_back(name);

                                      if (name == "parameterValues")
                                          keyed = bridge.currentPayload();
                                  },
                                  EA::Listener::Modes::TriggerOnEvent};

    bridge.dispatch("editText",
                    Miro::toJSON(WebParameterText {.id = "Mode", .text = "Hot"}));
    check(plugin.params.mode.getIndex() == 2);

    api.poll();
    check(emitted.size() == 2);
    check(keyed["items"].isArray());
    auto mode = static_cast<size_t>(plugin.parameters().indexOf("Mode"));
    check(keyed["items"][mode]["text"].asString() == "Hot");

    bridge.dispatch("resetParam", Miro::toJSON(WebParameterId {.id = "Mode"}));
    check(plugin.params.mode.getIndex() == 0);

    plugin.params.gain.setValue(-3.f);
    bridge.dispatch("resetAll", {});
    check(near(plugin.params.gain.getValue(), 0.0));

    auto values = bridge.dispatch("getParameterValues", {})["items"];
    check(values.asArray().size()
          == static_cast<size_t>(plugin.parameters().size()));

    plugin.setHostEditListener(nullptr);
};
} // namespace
