// A product's own APIs on a WebEditor: request/response, async commands,
// polled and pushed events, bound to a Miro bridge as the page's is (bind()),
// and the timers PagePolls runs them on, through the message loop. No WebView
// is constructed.

#include "TestPlugins.h"

#include <MakeASound/Plugin/Web/WebEditor.h>

#include <eacp/Core/Core.h>
#include <NanoTest/NanoTest.h>

#include <string>
#include <vector>

using namespace nano;
using namespace TestPlugins;
using namespace MakeASound::Web;

namespace
{
struct SpectrumRequest
{
    int bins = 0;

    MIRO_REFLECT(bins)
};

struct SpectrumFrame
{
    Vector<double> magnitudes;
    int sequence = 0;

    MIRO_REFLECT(magnitudes, sequence)
};

// A spectrum channel as a product would add one: a command to size it, a
// command reading the latest frame, and a frames event the editor's poll
// publishes when the analyser (here, the test) produced a new one.
struct SpectrumApi
{
    void reflect(Miro::ApiReflector& r)
    {
        using T = SpectrumApi;

        r.commands<&T::setBins, &T::getFrame>();
        r.events<&T::frames>();
    }

    void setBins(const SpectrumRequest& request) { bins = request.bins; }

    SpectrumFrame getFrame() { return latest; }

    void poll()
    {
        ++polls;

        if (latest.sequence == published)
            return;

        published = latest.sequence;
        frames.publish(latest);
    }

    void produce(double magnitude)
    {
        latest.magnitudes.clear();

        for (auto i = 0; i < bins; ++i)
            latest.magnitudes.add(magnitude);

        ++latest.sequence;
    }

    Miro::Event<SpectrumFrame> frames;
    SpectrumFrame latest;
    int bins = 0;
    int published = 0;
    int polls = 0;
};

struct FileRequest
{
    std::string extension;

    MIRO_REFLECT(extension)
};

struct FileChoice
{
    std::string path;

    MIRO_REFLECT(path)
};

struct FileList
{
    Vector<std::string> paths;

    MIRO_REFLECT(paths)
};

// A file picker: an async command answered when the "dialog" closes, and a
// recent-files event pushed the moment one is picked. No poll().
struct FilePickerApi
{
    void reflect(Miro::ApiReflector& r)
    {
        using T = FilePickerApi;

        r.commands<&T::pickFile>();
        r.events<&T::recent>();
    }

    void pickFile(const FileRequest& request, Miro::Completer<FileChoice> done)
    {
        pending.push_back({request.extension, std::move(done)});
    }

    void close(const std::string& path)
    {
        auto [extension, done] = std::move(pending.front());
        pending.erase(pending.begin());

        auto chosen = path + "." + extension;
        done.resolve({.path = chosen});

        auto list = recent.snapshot();
        list.paths.add(chosen);
        recent.publish(std::move(list));
    }

    struct Pending
    {
        std::string extension;
        Miro::Completer<FileChoice> done;
    };

    Miro::Event<FileList> recent;
    std::vector<Pending> pending;
};

// Every event a bridge emits, by name.
struct EmitLog
{
    explicit EmitLog(Miro::Bridge& bridgeToUse)
        : bridge(bridgeToUse)
    {
    }

    int count(const std::string& name) const
    {
        auto total = 0;

        for (const auto& [event, payload]: events)
            total += event == name ? 1 : 0;

        return total;
    }

    Miro::Bridge& bridge;
    std::vector<std::pair<std::string, Miro::JSON>> events;
    EA::Listener listener {bridge.onEmit,
                           [this]
                           {
                               events.emplace_back(
                                   std::string {bridge.currentEvent()},
                                   bridge.currentPayload());
                           },
                           EA::Listener::Modes::TriggerOnEvent};
};

WebEditorOptions noPage()
{
    return {.category = "WebEditorApiTests"};
}

auto tCommands = test("Web/useBindsAProductsCommandsBesideTheParameters") = []
{
    auto plugin = GainPlugin {};
    auto spectrum = SpectrumApi {};
    auto editor = WebEditor(plugin, noPage());
    editor.use(spectrum);

    auto bridge = Miro::Bridge {};
    editor.bind(bridge);

    bridge.dispatch("setBins", Miro::toJSON(SpectrumRequest {.bins = 4}));
    check(spectrum.bins == 4);

    spectrum.produce(0.5);
    auto frame = bridge.dispatch("getFrame", {});
    check(frame["sequence"].asNumber() == 1);
    check(frame["magnitudes"].asArray().size() == 4);

    // The parameters are still the page's.
    auto list = bridge.dispatch("getParameters", {})["parameters"];
    check(list.asArray().size() == static_cast<size_t>(plugin.parameters().size()));
};

auto tPoll = test("Web/pollCallsEveryPollingApiAndPublishesWhatItGathered") = []
{
    auto plugin = GainPlugin {};
    auto spectrum = SpectrumApi {};
    auto editor = WebEditor(plugin, noPage());
    editor.use(spectrum, {.pollHz = 60});

    auto bridge = Miro::Bridge {};
    editor.bind(bridge);
    auto log = EmitLog(bridge);

    editor.poll();
    check(spectrum.polls == 1);
    check(log.events.empty());

    spectrum.bins = 2;
    spectrum.produce(0.25);
    plugin.params.gain.setValue(-6.f);
    editor.poll();

    check(spectrum.polls == 2);
    check(log.count("frames") == 1);
    check(log.count("values") == 1);
    check(log.count("parameterValues") == 1);

    editor.poll();
    check(log.events.size() == 3);
};

auto tAsync = test("Web/anAsyncCommandAnswersWhenTheProductDoes") = []
{
    auto plugin = GainPlugin {};
    auto picker = FilePickerApi {};
    auto editor = WebEditor(plugin, noPage());
    editor.use(picker);

    auto bridge = Miro::Bridge {};
    editor.bind(bridge);
    auto log = EmitLog(bridge);

    auto answered = std::string {};
    auto failed = false;

    bridge.dispatchAsync("pickFile",
                         Miro::toJSON(FileRequest {.extension = "wav"}),
                         [&](const Miro::JSON& result, const std::string* error)
                         {
                             failed = error != nullptr;

                             if (!failed)
                                 answered = result["path"].asString();
                         });

    check(answered.empty());
    check(picker.pending.size() == 1);

    // Pushed: the event reaches the page with no poll.
    picker.close("Kick");

    check(!failed);
    check(answered == "Kick.wav");
    check(log.count("recent") == 1);
    check(log.events.back().second["paths"][size_t {0}].asString() == "Kick.wav");

    // An API with no poll() adds no poll; the parameters' still runs.
    editor.poll();
    check(log.count("recent") == 1);
};

auto tAddPoll = test("Web/addPollRunsACallOfTheProductsOwn") = []
{
    auto plugin = GainPlugin {};
    auto editor = WebEditor(plugin, noPage());
    auto calls = 0;

    editor.addPoll([&] { ++calls; }, 5);
    editor.poll();
    editor.poll();

    check(calls == 2);
};

auto tRates = test("Web/pagePollsRunEachAtItsOwnRateUntilStopped") = []
{
    auto polls = PagePolls {};
    auto fast = 0;
    auto slow = 0;

    polls.add([&] { ++fast; }, 100);
    polls.add([&] { ++slow; }, 4);
    check(polls.size() == 2);
    check(!polls.isRunning());

    polls.start();
    check(polls.isRunning());
    check(fast == 1 && slow == 1);

    eacp::Threads::runEventLoopFor(eacp::Time::MS {600});

    // A loaded machine runs timers late, never early: loose bounds.
    check(slow >= 2 && slow <= 4);
    check(fast > slow * 3);

    polls.stop();
    auto stoppedFast = fast;
    auto stoppedSlow = slow;

    eacp::Threads::runEventLoopFor(eacp::Time::MS {300});
    check(fast == stoppedFast);
    check(slow == stoppedSlow);
};

auto tLate = test("Web/aPollAddedWhileRunningStartsAtOnce") = []
{
    auto polls = PagePolls {};
    auto calls = 0;

    polls.start();
    polls.add([&] { ++calls; }, 50);
    check(calls == 1);

    check(eacp::Threads::runEventLoopUntil([&] { return calls >= 3; },
                                           eacp::Time::MS {5000}));

    polls.start();
    polls.stop();
    auto stopped = calls;
    eacp::Threads::runEventLoopFor(eacp::Time::MS {100});
    check(calls == stopped);
};
} // namespace
