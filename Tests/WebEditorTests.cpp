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

    auto mode = plugin.parameters().indexOf("Mode");
    check(list[mode].steps == 2);
    check(list[mode].choices.size() == 3);
    check(list[mode].choices[1] == "Warm");

    auto bypass = plugin.parameters().indexOf("Bypass");
    check(list[bypass].steps == 1);
    check(list[bypass].choices.empty());
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

    bridge.dispatch("edit", Miro::toJSON(WebParameterEdit {.id = "Gain"}));
    api.poll();
    api.endAllEdits();
};
} // namespace
