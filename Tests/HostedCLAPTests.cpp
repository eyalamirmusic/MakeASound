// Hosting CLAP plugins: the fixture plugins (Tests/HostFixtures, as CLAP) loaded
// by descriptor id through the FormatManager, their ports and parameters (plain
// ranges, text through clap.params, renames, latency through a restart), output
// parameters, MIDI both ways, the state round trip and the fallback playhead; and
// a stub plugin written against the C API alone, which checks every call this
// host makes against the spec's rules (the thread, active and processing state
// each call requires) and drives what the fixtures cannot: gestures, the CLAP
// note dialect, requests from the audio thread, restarts, timers, and the order
// things are torn down in. The fixtures are MakeASound's own adapter, so the
// stub is what keeps them from masking a host that breaks the spec.

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/CLAP/CLAPHostedPlugin.h>
#include <MakeASound/Hosting/CLAP/CLAPFormat.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef MAKEASOUND_HOST_FIXTURE_CLAP_DIR
#error "MAKEASOUND_HOST_FIXTURE_CLAP_DIR must name the fixtures' CLAP folder"
#endif

namespace HostedCLAPTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;

namespace
{
namespace fs = std::filesystem;

constexpr auto blockSize = 64;

struct DefaultFormats : FormatManager
{
    DefaultFormats() { addDefaultFormats(); }
};

std::string fixture(const char* name)
{
    const auto folder = fs::path(MAKEASOUND_HOST_FIXTURE_CLAP_DIR);

    return (folder / (std::string(name) + ".clap")).string();
}

Hosting::PluginDescription describe(const FormatManager& formats, const char* name)
{
    auto scan = formats.scanTarget(fixture(name)).scan;
    check(scan.classes.size() == 1);
    return scan.classes.empty() ? Hosting::PluginDescription() : scan.classes[0];
}

OwningPointer<HostedPlugin> load(const FormatManager& formats, const char* name)
{
    auto result = formats.load(describe(formats, name), {.name = "Hosting Tests"});
    check(result.error.empty());
    check(result.plugin != nullptr);
    return std::move(result.plugin);
}

int indexNamed(const Plugin& plugin, std::string_view name)
{
    const auto& list = plugin.parameters();

    for (auto i = 0; i < list.size(); ++i)
        if (list[i].name() == name)
            return i;

    return -1;
}

void copyState(HostedPlugin& from, HostedPlugin& to)
{
    to.loadState(from.saveState(StateContext::Session), StateContext::Session);
}

bool near(double a, double b, double tolerance = 1e-4)
{
    return std::abs(a - b) < tolerance;
}

// A context over the plugin's own buses, every one supplied unless told.
struct Block
{
    explicit Block(Plugin& pluginToUse,
                   bool supplyAux = true,
                   double sampleRate = 48000.0)
        : plugin(pluginToUse)
    {
        auto layout = plugin.getBusLayout();
        context.prepare(layout);

        for (auto i = 0; i < layout.inputs.size(); ++i)
            if (supplyAux || layout.inputs[i].isMain)
                context.inputs[i].setSize(layout.inputs[i].numChannels, blockSize);

        for (auto i = 0; i < layout.outputs.size(); ++i)
            context.outputs[i].setSize(layout.outputs[i].numChannels, blockSize);

        plugin.prepare(
            {.sampleRate = sampleRate, .maxBlockSize = blockSize, .layout = layout});
    }

    void fillInput(int bus, float value)
    {
        for (auto channel: context.inputs[bus])
            for (auto& sample: channel)
                sample = value;
    }

    void run()
    {
        context.clearMidiOut();
        plugin.process(context);
    }

    Plugin& plugin;
    ProcessContext context;
};

struct EditRecorder : HostEditListener
{
    void beginParameterEdit(int index) noexcept override
    {
        events.add("begin " + std::to_string(index));

        if (onBegin)
            onBegin(index);
    }

    void performParameterEdit(int index, float value) noexcept override
    {
        events.add("perform " + std::to_string(index));
        lastValue = value;
    }

    void endParameterEdit(int index) noexcept override
    {
        events.add("end " + std::to_string(index));
    }

    void parameterInfoChanged() noexcept override { ++infos; }
    void latencyChanged() noexcept override { ++latencies; }
    void parameterValuesChanged() noexcept override { ++valueChanges; }
    void stateChanged() noexcept override { ++stateChanges; }

    Vector<std::string> events;
    float lastValue = -1.f;
    int infos = 0;
    int latencies = 0;
    int valueChanges = 0;
    int stateChanges = 0;

    // Run at each begin, as a host reacting to a gesture might.
    std::function<void(int index)> onBegin;
};

bool eventsAre(const Vector<std::string>& events,
               std::initializer_list<const char*> expected)
{
    if (events.size() != static_cast<int>(expected.size()))
        return false;

    auto i = 0;

    for (const auto* each: expected)
        if (events[i++] != each)
            return false;

    return true;
}

// Every gesture whole: per parameter begin, performs, end, never nested, and
// none left open.
bool gesturesBalanced(const Vector<std::string>& events)
{
    auto open = std::set<std::string>();

    for (const auto& event: events)
    {
        auto space = event.find(' ');
        auto kind = event.substr(0, space);
        auto index = event.substr(space + 1);

        if (kind == "begin" && !open.insert(index).second)
            return false;

        if ((kind == "perform" || kind == "end") && open.count(index) == 0)
            return false;

        if (kind == "end")
            open.erase(index);
    }

    return open.empty();
}

int countOf(const Vector<std::string>& events, const std::string& wanted)
{
    auto count = 0;

    for (const auto& event: events)
        count += event == wanted ? 1 : 0;

    return count;
}

// A CLAP plugin by hand, over the C API alone. Every call it gets is checked
// against what the spec requires of the host at that point, and each breach is
// kept in `violations`, which every stub test expects empty:
//
//   init, activate, deactivate, destroy, on_main_thread, clap.params' reads,
//   clap.state, clap.latency, timers           [main-thread]
//   start_processing, stop_processing, process  [audio-thread]
//   flush                                       [active ? audio-thread : main-thread]
//   clap.tail                                   [main-thread, audio-thread]
//   flush never re-entered; activate only inactive; deactivate only active and not processing;
//   start_processing only active; process only active and processing; destroy
//   only inactive; nothing at all after destroy; input events sorted by time.
//
// Parameters (clap_id: name, range, flags):
//
//   10 Gain    0..2, default 1   automatable          (scales the output)
//   20 Mode    0..3, default 0   stepped enum
//   30 Hidden  0..1              hidden
//   40 Meter   0..1              read-only
//
// No audio input, a stereo output, a note input preferring the CLAP dialect (or
// `noteInputs` of them, `failNoteInput` refusing get()) and a note output. Its
// state is "gain=<value>", its tail 480 samples.
namespace Stub
{
constexpr auto gainId = clap_id {10};
constexpr auto modeId = clap_id {20};
constexpr auto hiddenId = clap_id {30};
constexpr auto meterId = clap_id {40};

clap_event_header headerOf(uint32_t size, uint16_t type, uint32_t time)
{
    return {size, time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
}

CLAPEvent paramValue(clap_id id, double value, uint32_t time = 0)
{
    auto event = CLAPEvent {};
    event.param = clap_event_param_value {};
    event.param.header =
        headerOf(sizeof(clap_event_param_value), CLAP_EVENT_PARAM_VALUE, time);
    event.param.param_id = id;
    event.param.note_id = -1;
    event.param.port_index = -1;
    event.param.channel = -1;
    event.param.key = -1;
    event.param.value = value;
    return event;
}

CLAPEvent gesture(clap_id id, bool begin)
{
    auto event = CLAPEvent {};
    event.gesture = clap_event_param_gesture {};
    event.gesture.header =
        headerOf(sizeof(clap_event_param_gesture),
                 static_cast<uint16_t>(begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                             : CLAP_EVENT_PARAM_GESTURE_END),
                 0);
    event.gesture.param_id = id;
    return event;
}

CLAPEvent noteOn(int16_t channel, int16_t key, double velocity, uint32_t time)
{
    auto event = CLAPEvent {};
    event.note = clap_event_note {};
    event.note.header = headerOf(sizeof(clap_event_note), CLAP_EVENT_NOTE_ON, time);
    event.note.note_id = -1;
    event.note.port_index = 0;
    event.note.channel = channel;
    event.note.key = key;
    event.note.velocity = velocity;
    return event;
}

struct Plugin
{
    Plugin()
    {
        clap.desc = &descriptor;
        clap.plugin_data = this;
        clap.init = [](const clap_plugin* p) { return of(p).init(); };
        clap.destroy = [](const clap_plugin* p) { of(p).destroy(); };
        clap.activate =
            [](const clap_plugin* p, double rate, uint32_t low, uint32_t high)
        { return of(p).activate(rate, low, high); };
        clap.deactivate = [](const clap_plugin* p) { of(p).deactivate(); };
        clap.start_processing = [](const clap_plugin* p)
        { return of(p).startProcessing(); };
        clap.stop_processing = [](const clap_plugin* p) { of(p).stopProcessing(); };
        clap.reset = [](const clap_plugin*) {};
        clap.process = [](const clap_plugin* p, const clap_process* data)
        { return of(p).process(*data); };
        clap.get_extension = [](const clap_plugin* p, const char* id)
        { return of(p).extension(id); };
        clap.on_main_thread = [](const clap_plugin* p) { of(p).onMainThread(); };

        params.count = [](const clap_plugin*) { return uint32_t {4}; };
        params.get_info =
            [](const clap_plugin* p, uint32_t index, clap_param_info* info)
        { return of(p).paramInfo(index, *info); };
        params.get_value = [](const clap_plugin* p, clap_id id, double* value)
        { return of(p).paramValue(id, *value); };
        params.value_to_text = [](const clap_plugin* p,
                                  clap_id,
                                  double value,
                                  char* buffer,
                                  uint32_t size)
        {
            of(p).expectMain("value_to_text");
            std::snprintf(buffer, size, "%.2f units", value);
            return true;
        };
        params.text_to_value =
            [](const clap_plugin* p, clap_id, const char* text, double* value)
        {
            of(p).expectMain("text_to_value");
            *value = std::strtod(text, nullptr);
            return true;
        };
        params.flush = [](const clap_plugin* p,
                          const clap_input_events* in,
                          const clap_output_events* out) { of(p).flush(*in, *out); };

        audioPorts.count = [](const clap_plugin*, bool isInput)
        { return isInput ? uint32_t {0} : uint32_t {1}; };
        audioPorts.get = [](const clap_plugin* p,
                            uint32_t index,
                            bool isInput,
                            clap_audio_port_info* info)
        {
            if (isInput || index != 0 || of(p).failAudioOutput)
                return false;

            *info = clap_audio_port_info {};
            info->id = 0;
            std::snprintf(info->name, sizeof info->name, "Out");
            info->flags = CLAP_AUDIO_PORT_IS_MAIN;
            info->channel_count = 2;
            info->port_type = CLAP_PORT_STEREO;
            info->in_place_pair = CLAP_INVALID_ID;
            return true;
        };

        notePorts.count = [](const clap_plugin* p, bool isInput)
        { return isInput ? of(p).noteInputs : uint32_t {1}; };
        notePorts.get = [](const clap_plugin* p,
                           uint32_t index,
                           bool isInput,
                           clap_note_port_info* info)
        {
            const auto& self = of(p);

            if (index >= (isInput ? self.noteInputs : uint32_t {1})
                || (isInput && static_cast<int>(index) == self.failNoteInput))
                return false;

            *info = clap_note_port_info {};
            info->id = index;
            info->supported_dialects =
                CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
            info->preferred_dialect =
                isInput ? CLAP_NOTE_DIALECT_CLAP : CLAP_NOTE_DIALECT_MIDI;
            std::snprintf(info->name,
                          sizeof info->name,
                          "%s",
                          isInput ? "Notes In" : "Notes Out");
            return true;
        };

        state.save = [](const clap_plugin* p, const clap_ostream* stream)
        {
            auto& self = of(p);
            self.expectMain("state save");
            auto text = "gain=" + std::to_string(self.gain);
            return stream->write(stream, text.data(), text.size())
                   == static_cast<int64_t>(text.size());
        };
        state.load = [](const clap_plugin* p, const clap_istream* stream)
        {
            auto& self = of(p);
            self.expectMain("state load");
            auto text = std::string();
            char chunk[64];

            while (true)
            {
                auto read = stream->read(stream, chunk, sizeof chunk);

                if (read <= 0)
                    break;

                text.append(chunk, static_cast<size_t>(read));
            }

            if (text.rfind("gain=", 0) != 0)
                return false;

            self.gain = std::strtod(text.c_str() + 5, nullptr);
            return true;
        };

        latency.get = [](const clap_plugin* p)
        {
            auto& self = of(p);
            self.expectMain("latency get");
            self.expect(self.active || self.activating,
                        "latency asked while neither active nor activating");
            return self.latencySamples;
        };

        timers.on_timer = [](const clap_plugin* p, clap_id id)
        { of(p).onTimer(id); };

        tail.get = [](const clap_plugin* p)
        {
            auto& self = of(p);
            self.expectAlive("tail get");
            self.expect(self.onMain() || self.onAudio(),
                        "tail get off the main and audio threads");
            return uint32_t {480};
        };
    }

    static Plugin& of(const clap_plugin* p)
    {
        return *static_cast<Plugin*>(p->plugin_data);
    }

    const clap_plugin* make(const clap_host* hostToUse)
    {
        host = hostToUse;
        return &clap;
    }

    template <typename Extension>
    const Extension* hostExtension(const char* id) const
    {
        return static_cast<const Extension*>(host->get_extension(host, id));
    }

    bool onMain() const
    {
        const auto* threads =
            hostExtension<clap_host_thread_check>(CLAP_EXT_THREAD_CHECK);
        return threads != nullptr && threads->is_main_thread(host)
               && !threads->is_audio_thread(host);
    }

    bool onAudio() const
    {
        const auto* threads =
            hostExtension<clap_host_thread_check>(CLAP_EXT_THREAD_CHECK);
        return threads != nullptr && threads->is_audio_thread(host)
               && !threads->is_main_thread(host);
    }

    void expect(bool ok, const std::string& what)
    {
        if (!ok)
            violations.push_back(what);
    }

    void expectAlive(const char* call)
    {
        expect(!destroyed, std::string(call) + " after destroy");
    }

    void expectMain(const char* call)
    {
        expectAlive(call);
        expect(onMain(), std::string(call) + " off the main thread");
    }

    void expectAudio(const char* call)
    {
        expectAlive(call);
        expect(onAudio(), std::string(call) + " off the audio thread");
    }

    bool init()
    {
        expectMain("init");
        calls.push_back("init");

        if (wantTimer)
        {
            const auto* support =
                hostExtension<clap_host_timer_support>(CLAP_EXT_TIMER_SUPPORT);
            expect(support != nullptr, "no clap.timer-support");

            if (support != nullptr)
                timerRegistered = support->register_timer(host, 5, &timer);
        }

        return !failInit;
    }

    void destroy()
    {
        expectMain("destroy");
        expect(!active, "destroy while active");
        calls.push_back("destroy");
        destroyed = true;
    }

    bool activate(double rate, uint32_t minFrames, uint32_t maxFrames)
    {
        expectMain("activate");
        expect(!active, "activate while active");
        expect(minFrames >= 1 && maxFrames >= minFrames, "activate with bad frames");
        calls.push_back("activate");

        activating = true;

        // Being activated is when a latency change may be told.
        if (latencySamples != reportedLatency)
        {
            if (const auto* told =
                    hostExtension<clap_host_latency>(CLAP_EXT_LATENCY))
                told->changed(host);

            reportedLatency = latencySamples;
        }

        activating = false;
        active = true;
        sampleRate = rate;
        maxBlock = maxFrames;
        return true;
    }

    void deactivate()
    {
        expectMain("deactivate");
        expect(active, "deactivate while inactive");
        expect(!processing, "deactivate while processing");
        calls.push_back("deactivate");
        active = false;
    }

    bool startProcessing()
    {
        expectAudio("start_processing");
        expect(active, "start_processing while inactive");
        expect(!processing, "start_processing while processing");
        calls.push_back("start");
        processing = true;
        return true;
    }

    void stopProcessing()
    {
        expectAudio("stop_processing");
        expect(active && processing, "stop_processing while not processing");
        calls.push_back("stop");
        processing = false;
    }

    void readInput(const clap_input_events& in)
    {
        auto previous = uint32_t {0};

        for (auto i = uint32_t {0}; i < in.size(&in); ++i)
        {
            const auto* header = in.get(&in, i);

            if (header == nullptr)
                continue;

            expect(header->time >= previous, "input events out of time order");
            previous = header->time;

            auto copy = CLAPEvent {};
            std::memcpy(&copy, header, std::min<size_t>(header->size, sizeof copy));
            received.push_back(copy);

            if (header->type == CLAP_EVENT_PARAM_VALUE)
            {
                const auto& value = copy.param;

                if (value.param_id == gainId)
                    gain = value.value;
                else if (value.param_id == modeId)
                    mode = value.value;
                else
                    expect(false, "a value for a parameter the host may not write");
            }
        }
    }

    clap_process_status process(const clap_process& data)
    {
        expectAudio("process");
        expect(active && processing, "process while not active and processing");
        expect(data.frames_count <= maxBlock, "a block longer than activated for");
        ++blocks;

        if (data.in_events != nullptr)
            readInput(*data.in_events);

        if (data.transport != nullptr)
            lastTransport = *data.transport;

        for (auto bus = uint32_t {0}; bus < data.audio_outputs_count; ++bus)
            for (auto ch = uint32_t {0}; ch < data.audio_outputs[bus].channel_count;
                 ++ch)
                std::fill_n(data.audio_outputs[bus].data32[ch],
                            data.frames_count,
                            static_cast<float>(gain));

        for (const auto& event: emitInProcess)
            data.out_events->try_push(data.out_events, &event.header);

        emitInProcess.clear();

        if (callbackFromProcess)
            host->request_callback(host);

        if (restartFromProcess)
            host->request_restart(host);

        // [main-thread] only: refused from here.
        if (unregisterTimerFromProcess)
            unregisteredFromProcess =
                hostExtension<clap_host_timer_support>(CLAP_EXT_TIMER_SUPPORT)
                    ->unregister_timer(host, timer);

        callbackFromProcess = restartFromProcess = false;
        unregisterTimerFromProcess = false;
        return CLAP_PROCESS_CONTINUE;
    }

    void flush(const clap_input_events& in, const clap_output_events& out)
    {
        expectAlive("flush");
        expect(active ? onAudio() : onMain(), "flush on the wrong thread");
        expect(!inFlush, "flush re-entered");
        inFlush = true;
        ++flushes;
        readInput(in);

        for (const auto& event: emitInFlush)
            out.try_push(&out, &event.header);

        emitInFlush.clear();

        if (duringFlush)
            std::exchange(duringFlush, nullptr)();

        inFlush = false;
    }

    const void* extension(const char* id)
    {
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &params;

        if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
            return &audioPorts;

        if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
            return &notePorts;

        if (std::strcmp(id, CLAP_EXT_STATE) == 0)
            return &state;

        if (std::strcmp(id, CLAP_EXT_LATENCY) == 0)
            return &latency;

        if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0)
            return &timers;

        if (std::strcmp(id, CLAP_EXT_TAIL) == 0)
            return &tail;

        return nullptr;
    }

    void onMainThread()
    {
        expectMain("on_main_thread");
        ++mainThreadCalls;
    }

    void onTimer(clap_id id)
    {
        expectMain("on_timer");
        expect(id == timer, "a tick for a timer never registered");
        ++ticks;

        if (duringTick)
        {
            std::exchange(duringTick, nullptr)();
            return;
        }

        // Unregistered from inside its own tick.
        if (ticks == 2)
            hostExtension<clap_host_timer_support>(CLAP_EXT_TIMER_SUPPORT)
                ->unregister_timer(host, timer);
    }

    bool paramInfo(uint32_t index, clap_param_info& info)
    {
        expectMain("get_info");
        info = clap_param_info {};

        auto name = [&info](const char* text)
        { std::snprintf(info.name, sizeof info.name, "%s", text); };

        switch (index)
        {
            case 0:
                info.id = gainId;
                info.flags = CLAP_PARAM_IS_AUTOMATABLE;
                info.max_value = 2.0;
                info.default_value = 1.0;
                name("Gain");
                return true;
            case 1:
                info.id = modeId;
                info.flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM
                             | CLAP_PARAM_IS_AUTOMATABLE;
                info.max_value = 3.0;
                name("Mode");
                return true;
            case 2:
                info.id = hiddenId;
                info.flags = CLAP_PARAM_IS_HIDDEN;
                info.max_value = 1.0;
                name("Hidden");
                return true;
            case 3:
                info.id = meterId;
                info.flags = CLAP_PARAM_IS_READONLY;
                info.max_value = 1.0;
                name("Meter");
                return true;
            default:
                return false;
        }
    }

    bool paramValue(clap_id id, double& value)
    {
        expectMain("get_value");

        switch (id)
        {
            case gainId:
                value = gain;
                return true;
            case modeId:
                value = mode;
                return true;
            case hiddenId:
            case meterId:
                value = 0.0;
                return true;
            default:
                return false;
        }
    }

    // The lifecycle calls alone, in order.
    std::string lifecycle() const
    {
        auto joined = std::string();

        for (const auto& call: calls)
            joined += (joined.empty() ? "" : " ") + call;

        return joined;
    }

    const CLAPEvent* receivedOfType(uint16_t type) const
    {
        for (const auto& event: received)
            if (event.header.type == type)
                return &event;

        return nullptr;
    }

    clap_plugin_descriptor descriptor {CLAP_VERSION,
                                       "com.makeasound.tests.stub",
                                       "Stub",
                                       "MakeASound Tests",
                                       "",
                                       "",
                                       "",
                                       "1.0.0",
                                       "",
                                       nullptr};
    clap_plugin clap {};
    clap_plugin_params params {};
    clap_plugin_audio_ports audioPorts {};
    clap_plugin_note_ports notePorts {};
    clap_plugin_state state {};
    clap_plugin_latency latency {};
    clap_plugin_timer_support timers {};
    clap_plugin_tail tail {};

    const clap_host* host = nullptr;

    bool failInit = false;
    bool wantTimer = false;
    bool failAudioOutput = false;
    uint32_t noteInputs = 1;
    int failNoteInput = -1;
    bool inFlush = false;
    bool active = false;
    bool activating = false;
    bool processing = false;
    bool destroyed = false;

    double sampleRate = 0.0;
    uint32_t maxBlock = 0;
    uint32_t latencySamples = 0;
    uint32_t reportedLatency = 0;

    double gain = 1.0;
    double mode = 0.0;

    std::vector<std::string> calls;
    std::vector<std::string> violations;
    std::vector<CLAPEvent> received;
    clap_event_transport lastTransport {};
    int blocks = 0;
    int flushes = 0;
    int mainThreadCalls = 0;

    std::vector<CLAPEvent> emitInProcess;
    std::vector<CLAPEvent> emitInFlush;
    bool callbackFromProcess = false;
    bool restartFromProcess = false;
    bool unregisterTimerFromProcess = false;
    bool unregisteredFromProcess = true;

    // Run once, from inside the next flush or tick.
    std::function<void()> duringFlush;
    std::function<void()> duringTick;

    clap_id timer = CLAP_INVALID_ID;
    bool timerRegistered = false;
    int ticks = 0;
};

OwningPointer<CLAPHostedPlugin> host(Plugin& stub)
{
    auto description = Hosting::PluginDescription();
    description.name = "Stub";
    description.classId = stub.descriptor.id;

    auto result = CLAPHostedPlugin::create(description,
                                           nullptr,
                                           [&stub](const clap_host* clapHost)
                                           { return stub.make(clapHost); },
                                           {.name = "Stub Host"});

    check(result.error.empty());
    check(result.plugin != nullptr);

    return OwningPointer<CLAPHostedPlugin>(
        static_cast<CLAPHostedPlugin*>(result.plugin.release()));
}

void checkClean(const Plugin& stub)
{
    for (const auto& violation: stub.violations)
        std::fprintf(stderr, "spec violation: %s\n", violation.c_str());

    check(stub.violations.empty());
}
} // namespace Stub

// ------------------------------------------------------------------ fixtures

auto tLoad = test("HostedCLAP/aPluginLoadsByItsDescriptorId") = []
{
    const auto formats = DefaultFormats();
    const auto description = describe(formats, "HostFixtureEffect");
    auto plugin = load(formats, "HostFixtureEffect");

    check(plugin->name() == "HostFixtureEffect");
    check(plugin->description().classId == description.classId);
    check(plugin->description().path == fixture("HostFixtureEffect"));
    check(plugin->description().format == PluginFormat::CLAP);
    check(!plugin->wantsHostBypass());

    auto* clap = dynamic_cast<CLAPHostedPlugin*>(plugin.get());
    check(clap != nullptr);
    check(clap->clapPlugin() != nullptr);
    check(clap->clapHost()->host_data == clap);
    check(!clap->isActive());
};

auto tPair = test("HostedCLAP/eachPluginOfAModuleLoadsByItsOwnId") = []
{
    const auto formats = DefaultFormats();
    const auto scan = formats.scanTarget(fixture("HostFixturePair")).scan;
    check(scan.classes.size() == 2);

    for (const auto& description: scan.classes)
    {
        auto result = formats.load(description);
        check(result.error.empty());
        check(result.plugin != nullptr && result.plugin->name() == description.name);
    }
};

auto tLoadErrors = test("HostedCLAP/aLoadThatCannotHappenSaysWhy") = []
{
    const auto formats = DefaultFormats();

    auto wrongClass = describe(formats, "HostFixtureEffect");
    wrongClass.classId = "com.nobody.nothing";
    auto noClass = formats.load(wrongClass);
    check(noClass.plugin == nullptr);
    check(noClass.error.find("no plugin with id") != std::string::npos);

    auto missing = wrongClass;
    missing.path = fixture("Missing");
    check(formats.load(missing).plugin == nullptr);
    check(!formats.load(missing).error.empty());

    auto made = CLAPHostedPlugin::create(
        wrongClass, nullptr, [](const clap_host*) { return nullptr; }, {});
    check(made.plugin == nullptr);
    check(!made.error.empty());
};

auto tBuses = test("HostedCLAP/thePortsAreThePluginsOwn") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    const auto layout = plugin->getBusLayout();

    check(layout.inputs.size() == 2);
    check(layout.inputs[0].numChannels == 2 && layout.inputs[0].isMain);
    check(layout.inputs[1].name == "Sidechain");
    check(layout.inputs[1].numChannels == 1 && !layout.inputs[1].isMain);
    check(layout.outputs.size() == 1);
    check(layout.outputs[0].numChannels == 2 && layout.outputs[0].isMain);
    check(layout.midiInputs.empty());
    check(layout.midiOutputs.empty());

    check(plugin->acceptsLayout(layout));

    auto fewer = layout;
    fewer.inputs.pop_back();
    check(!plugin->acceptsLayout(fewer));

    auto wider = layout;
    wider.outputs[0].numChannels = 6;
    check(!plugin->acceptsLayout(wider));
};

auto tParameters = test("HostedCLAP/parametersCarryThePluginsIdsAndRanges") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto* clap = dynamic_cast<CLAPHostedPlugin*>(plugin.get());
    const auto& list = plugin->parameters();

    // The fixture's eight, plus the bypass its own wrapper added.
    check(list.size() == 9);
    check(list.identityProblems().empty());
    check(list.bypassIndex() >= 0);
    check(list[list.bypassIndex()].isBypass());

    for (auto i = 0; i < list.size(); ++i)
    {
        const auto& entry = list.entry(i);
        check(entry.id == std::to_string(entry.hostId));
        check(clap->indexOfParamId(entry.hostId) == i);
    }

    // A skewed range is published normalized, so 0..1 here, its text the
    // plugin's: its skew and unit.
    auto cutoff = indexNamed(*plugin, "Cutoff");
    check(cutoff >= 0);
    check(list[cutoff].minValue() == 0.f && list[cutoff].maxValue() == 1.f);
    check(list[cutoff].numSteps() == 0);

    auto text = list[cutoff].valueToText(list[cutoff].getValue());
    check(text.find("1000") != std::string::npos);
    check(text.find("Hz") != std::string::npos);
    check(text.find("Hz") == text.rfind("Hz"));
    check(near(list[cutoff].textToValue(text), list[cutoff].getValue(), 1e-3));

    // Whole-number steps are published plain: the parameter's own range.
    auto latency = indexNamed(*plugin, "Latency");
    check(list[latency].minValue() == 0.f && list[latency].maxValue() == 1024.f);
    check(list[latency].numSteps() == 1024);
    check(!list[latency].isBoolean());

    auto alt = indexNamed(*plugin, "Alt Names");
    check(list[alt].numSteps() == 1);
    check(list[alt].isBoolean());
};

auto tAudio = test("HostedCLAP/aBlockRunsAndOutputParametersComeBack") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto* clap = dynamic_cast<CLAPHostedPlugin*>(plugin.get());
    auto block = Block(*plugin);

    check(clap->isActive());

    block.fillInput(0, 0.5f);
    block.run();

    check(near(block.context.outputs[0][0][0], 0.5));
    check(near(block.context.outputs[0][1][blockSize - 1], 0.5));

    auto peak = indexNamed(*plugin, "Peak");
    check(near(plugin->parameters()[peak].getValue(), 0.5));
};

auto tOutputsUntold =
    test("HostedCLAP/outputValuesOutsideAGestureAreCachedButNotTold") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);

    auto block = Block(*plugin);
    auto peak = indexNamed(*plugin, "Peak");
    auto tempo = indexNamed(*plugin, "Seen Tempo");
    const auto& list = plugin->parameters();

    // Each block moves Peak and Seen Tempo, values the plugin sends with no
    // gesture around them.
    auto fallback = Playhead();

    for (auto [level, bpm]: {std::pair {0.25f, 90.0}, std::pair {0.75f, 150.0}})
    {
        fallback.bpm = bpm;
        plugin->setFallbackPlayhead(fallback);
        block.fillInput(0, level);
        block.run();
        plugin->syncEditorWithProcessor();

        check(near(list[peak].getValue(), level));
        check(near(list[tempo].getValue(), bpm / 1000.0));
    }

    // Served on the loop as well: still nothing told.
    eacp::Threads::runEventLoopFor(eacp::Time::MS {50});

    check(listener.events.empty());
    check(listener.valueChanges == 0);

    plugin->setHostEditListener(nullptr);
};

auto tSidechain = test("HostedCLAP/theSidechainReachesThePlugin") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto block = Block(*plugin);

    block.fillInput(0, 0.5f);
    block.fillInput(1, 0.25f);
    block.run();

    check(near(plugin->parameters()[indexNamed(*plugin, "Sidechain")].getValue(),
               0.25));
};

auto tUnsupplied = test("HostedCLAP/aBusTheCallerDoesNotSupplyIsSilence") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto block = Block(*plugin, false);

    check(block.context.inputs[1].isEmpty());

    block.fillInput(0, 0.25f);
    block.run();

    check(near(block.context.outputs[0][0][0], 0.25));
    check(plugin->parameters()[indexNamed(*plugin, "Sidechain")].getValue() == 0.f);

    // A longer block than activated for is refused, its output silence.
    block.context.outputs[0].setSize(2, blockSize * 2);
    block.context.outputs[0].fill(1.f);
    block.run();
    check(block.context.outputs[0][0][0] == 0.f);
};

auto tHostEdit =
    test("HostedCLAP/aHostEditReachesTheProcessorAndALatencyRestarts") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);

    auto block = Block(*plugin);
    auto latency = indexNamed(*plugin, "Latency");

    check(plugin->latencySamples() == 0);
    check(!plugin->takeLatencyChanged());

    // Plain, as the plugin publishes it: the next block carries it, the plugin
    // asks for a callback, then a restart, and the reactivation tells the
    // latency.
    plugin->parameters()[latency].setValue(512.f);
    block.run();
    plugin->syncEditorWithProcessor();

    check(plugin->takeLatencyChanged());
    check(!plugin->takeLatencyChanged());
    check(plugin->latencySamples() == 512);
    check(listener.latencies == 1);

    // Still processing after the restart.
    block.fillInput(0, 0.5f);
    block.run();
    check(near(block.context.outputs[0][0][0], 0.5));

    plugin->setHostEditListener(nullptr);
};

auto tTail = test("HostedCLAP/aTailIsReadFiniteOrInfinite") = []
{
    const auto formats = DefaultFormats();

    check(load(formats, "HostFixtureEffect")->tailSamples() == 4800);
    check(load(formats, "HostFixtureInstrument")->tailSamples()
          == Plugin::infiniteTail);
};

auto tAudioThread = test("HostedCLAP/aValueFromTheAudioThreadLandsInTheBlock") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto cutoff = indexNamed(*plugin, "Cutoff");
    auto latency = indexNamed(*plugin, "Latency");

    check(!plugin->setParameterFromAudioThread(cutoff, 0.3f));

    auto block = Block(*plugin);

    check(!plugin->setParameterFromAudioThread(-1, 0.3f));
    check(!plugin->setParameterFromAudioThread(plugin->parameters().size(), 0.3f));

    // A second write before the block replaces the first.
    check(plugin->setParameterFromAudioThread(cutoff, 0.9f));
    check(plugin->setParameterFromAudioThread(cutoff, 0.3f));
    check(near(plugin->parameters()[cutoff].getValue(), 0.3));

    // Normalized in, the plain value out: a quarter of 0..1024.
    check(plugin->setParameterFromAudioThread(latency, 0.25f));
    check(plugin->parameters()[latency].getValue() == 256.f);

    block.run();

    auto fresh = load(formats, "HostFixtureEffect");
    copyState(*plugin, *fresh);
    check(near(fresh->parameters()[cutoff].getValue(), 0.3));
    check(fresh->parameters()[latency].getValue() == 256.f);
};

auto tActiveEdit = test("HostedCLAP/anEditWhileActiveRidesTheNextBlock") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto cutoff = indexNamed(*plugin, "Cutoff");
    auto block = Block(*plugin);

    plugin->parameters()[cutoff].setValue(0.7f);
    block.run();

    auto fresh = load(formats, "HostFixtureEffect");
    copyState(*plugin, *fresh);
    check(near(fresh->parameters()[cutoff].getValue(), 0.7));
};

auto tRename = test("HostedCLAP/aRenameIsReadBackInPlace") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);

    auto cutoff = indexNamed(*plugin, "Cutoff");
    auto hostId = plugin->parameters().entry(cutoff).hostId;
    auto alt = indexNamed(*plugin, "Alt Names");

    plugin->parameters()[alt].setValue(1.f);
    check(!plugin->takeParameterInfoChanged());

    // The fixture renames when a state with Alt Names on is loaded.
    copyState(*plugin, *plugin);

    check(plugin->takeParameterInfoChanged());
    check(!plugin->takeParameterInfoChanged());
    check(listener.infos >= 1);
    check(plugin->parameters()[cutoff].name() == "Frequency");
    check(plugin->parameters().entry(cutoff).displayName == "Frequency");
    check(plugin->parameters().entry(cutoff).hostId == hostId);
    check(indexNamed(*plugin, "Frequency") == cutoff);

    // A load the host asked for is not news to it.
    check(listener.valueChanges == 0);

    plugin->setHostEditListener(nullptr);
};

auto tState = test("HostedCLAP/theStateRoundTripsThroughAFreshInstance") = []
{
    const auto formats = DefaultFormats();
    auto source = load(formats, "HostFixtureEffect");
    auto cutoff = indexNamed(*source, "Cutoff");
    auto latency = indexNamed(*source, "Latency");

    // Never prepared: each edit reaches the plugin through a flush.
    source->parameters()[cutoff].setValue(0.2f);
    source->parameters()[latency].setValue(300.f);

    auto state = source->saveState(StateContext::Preset);
    check(!state.empty());

    auto copy = load(formats, "HostFixtureEffect");
    copy->loadState(state, StateContext::Preset);

    check(near(copy->parameters()[cutoff].getValue(), 0.2));
    check(copy->parameters()[latency].getValue() == 300.f);

    copy->loadState({}, StateContext::Preset);
    check(near(copy->parameters()[cutoff].getValue(), 0.2));
};

auto tRate = test("HostedCLAP/aFractionalRateReachesThePluginUnrounded") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto fraction = indexNamed(*plugin, "Rate Fraction");
    const auto& list = plugin->parameters();

    auto block = Block(*plugin, true, 44055.94);
    block.run();
    check(near(list[fraction].getValue(), 0.94));

    auto whole = Block(*plugin, true, 48000.0);
    whole.run();
    check(list[fraction].getValue() == 0.f);
};

auto tPlayhead = test("HostedCLAP/theTransportIsTheFallbackUnlessTheHostHasOne") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto tempo = indexNamed(*plugin, "Seen Tempo");
    auto playing = indexNamed(*plugin, "Seen Playing");
    const auto& list = plugin->parameters();

    auto block = Block(*plugin);
    block.run();
    check(near(list[tempo].getValue(), 0.12));
    check(list[playing].getValue() == 0.f);

    auto fallback = Playhead();
    fallback.isPlaying = true;
    fallback.bpm = 140.0;
    plugin->setFallbackPlayhead(fallback);

    block.run();
    check(near(list[tempo].getValue(), 0.14));
    check(list[playing].getValue() == 1.f);

    // A host's own transport wins.
    block.context.playhead.isValid = true;
    block.context.playhead.bpm = 90.0;
    block.context.playhead.isPlaying = false;
    block.run();
    check(near(list[tempo].getValue(), 0.09));
    check(list[playing].getValue() == 0.f);
};

auto tMidi = test("HostedCLAP/midiGoesInAndComesBackOut") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureInstrument");
    const auto layout = plugin->getBusLayout();

    check(layout.inputs.empty());
    check(layout.midiInputs.size() == 1);
    check(layout.midiOutputs.size() == 1);

    auto block = Block(*plugin);
    block.run();
    check(block.context.outputs[0][0][0] == 0.f);

    block.context.midiIn[0].add(MIDI::Event::noteOn(3, 60, 0.5f, 7));
    block.run();
    block.context.midiIn[0].clear();

    check(near(block.context.outputs[0][0][0], 0.8));
    check(block.context.midiOut[0].size() == 1);

    if (block.context.midiOut[0].size() == 1)
    {
        const auto& echoed = block.context.midiOut[0][0];
        check(echoed.sampleOffset == 7);
        check(echoed.channel == 3);
        check(echoed.asNoteOn() != nullptr && echoed.asNoteOn()->pitch == 60);
        check(echoed.asNoteOn() != nullptr
              && near(echoed.asNoteOn()->velocity, 0.5, 0.01));
    }

    block.context.midiIn[0].add(MIDI::Event::noteOff(3, 60, 0.f, 0));
    block.context.midiIn[0].add(MIDI::Event::controlChange(1, 7, 0.5f, 3));
    block.run();
    check(block.context.outputs[0][0][blockSize - 1] == 0.f);
    check(block.context.midiOut[0].size() == 2);

    if (block.context.midiOut[0].size() == 2)
    {
        check(block.context.midiOut[0][0].isNoteOff());
        check(block.context.midiOut[0][1].asControlChange() != nullptr);
        check(block.context.midiOut[0][1].sampleOffset == 3);
    }
};

auto tSysEx = test("HostedCLAP/sysExGoesInAndComesBackOut") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureInstrument");
    auto block = Block(*plugin);

    const auto shortOne =
        std::array<uint8_t, 6> {0xf0, 0x41, 0x10, 0x42, 0x12, 0xf7};
    auto longest = std::array<uint8_t, MIDI::SysEx::maxBytes> {};
    longest.front() = 0xf0;

    for (auto i = size_t {1}; i + 1 < longest.size(); ++i)
        longest[i] = static_cast<uint8_t>(i & 0x7f);

    longest.back() = 0xf7;

    block.context.midiIn[0].add(
        MIDI::Event::sysEx(shortOne.data(), static_cast<int>(shortOne.size()), 5));
    block.context.midiIn[0].add(MIDI::Event::noteOn(0, 64, 0.5f, 6));
    block.context.midiIn[0].add(
        MIDI::Event::sysEx(longest.data(), static_cast<int>(longest.size()), 9));
    block.run();
    block.context.midiIn[0].clear();

    const auto& out = block.context.midiOut[0];
    check(out.size() == 3);

    if (out.size() != 3)
        return;

    const auto* first = out[0].asSysEx();
    check(first != nullptr && out[0].sampleOffset == 5);
    check(first != nullptr && first->size == 6
          && std::equal(shortOne.begin(), shortOne.end(), first->data.begin()));

    check(out[1].isNoteOn());

    const auto* last = out[2].asSysEx();
    check(last != nullptr && out[2].sampleOffset == 9);
    check(last != nullptr && last->size == MIDI::SysEx::maxBytes
          && last->data == longest);
};

auto tTypedText = test("HostedCLAP/malformedTextAUserTypesNeverThrows") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    const auto& cutoff = plugin->parameters()[indexNamed(*plugin, "Cutoff")];

    for (auto typed: {std::string {"\xC3"},
                      std::string {"\xFF\xFE"},
                      std::string {"\xF0\x9F\x98"},
                      std::string {"\xED\xA0\x80"},
                      std::string {"1000\xC3 Hz"},
                      std::string {}})
    {
        auto value = cutoff.textToValue(typed);
        check(std::isfinite(value) && value >= 0.f && value <= 1.f);
    }
};

auto tHostName = test("HostedCLAP/anyHostNameLoads") = []
{
    const auto formats = DefaultFormats();
    const auto description = describe(formats, "HostFixtureEffect");

    for (auto name: {std::string {"Host \xFF\xFE"},
                     std::string {"\xC3"},
                     std::string(300, 'h'),
                     std::string {}})
    {
        auto result = formats.load(description, {.name = name});
        check(result.error.empty());
        check(result.plugin != nullptr);
    }
};

// ---------------------------------------------------------------- the stub

auto tLifecycle = test("HostedCLAP/theLifecycleFollowsTheSpec") = []
{
    auto stub = Stub::Plugin();

    {
        auto plugin = Stub::host(stub);
        check(stub.lifecycle() == "init");

        auto block = Block(*plugin);
        check(stub.lifecycle() == "init activate");
        check(stub.sampleRate == 48000.0 && stub.maxBlock == blockSize);

        // Processing starts lazily, on the audio thread, with the first block.
        block.run();
        block.run();
        check(stub.lifecycle() == "init activate start");
        check(stub.blocks == 2);

        // A prepare stops, deactivates and activates again.
        plugin->prepare({.sampleRate = 96000.0,
                         .maxBlockSize = blockSize,
                         .layout = plugin->getBusLayout()});
        check(stub.lifecycle() == "init activate start stop deactivate activate");
        check(stub.sampleRate == 96000.0);

        block.run();
    }

    // Torn down in the spec's order: stop, deactivate, then destroy.
    check(stub.lifecycle()
          == "init activate start stop deactivate activate start stop deactivate "
             "destroy");
    Stub::checkClean(stub);
};

auto tNeverPrepared = test("HostedCLAP/aPluginNeverPreparedIsOnlyDestroyed") = []
{
    auto stub = Stub::Plugin();

    {
        auto plugin = Stub::host(stub);
        auto block = ProcessContext();
        block.prepare(plugin->getBusLayout());
        block.outputs[0].setSize(2, blockSize);
        block.outputs[0].fill(1.f);

        // Not active: no block reaches it, and the output is silence.
        plugin->process(block);
        check(block.outputs[0][0][0] == 0.f);
        check(!plugin->setParameterFromAudioThread(0, 0.5f));
    }

    check(stub.lifecycle() == "init destroy");
    check(stub.blocks == 0);
    Stub::checkClean(stub);
};

auto tInitFails = test("HostedCLAP/aPluginThatFailsInitIsDestroyedAndSaysWhy") = []
{
    auto stub = Stub::Plugin();
    stub.failInit = true;

    auto result = CLAPHostedPlugin::create({},
                                           nullptr,
                                           [&stub](const clap_host* clapHost)
                                           { return stub.make(clapHost); },
                                           {});

    check(result.plugin == nullptr);
    check(result.error.find("initialise") != std::string::npos);
    check(stub.lifecycle() == "init destroy");
    Stub::checkClean(stub);
};

auto tStubParameters = test("HostedCLAP/hiddenAndReadOnlyParametersAreKeptOut") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    const auto& list = plugin->parameters();

    // Hidden is left out; the rest keep the plugin's order.
    check(list.size() == 3);
    check(list[0].name() == "Gain");
    check(list[1].name() == "Mode");
    check(list[2].name() == "Meter");
    check(plugin->indexOfParamId(Stub::hiddenId) == -1);
    check(list.identityProblems().empty());

    // Gain: continuous, its plain range, the value a view onto it.
    check(list[0].minValue() == 0.f && list[0].maxValue() == 2.f);
    check(list[0].getValue() == 1.f);
    check(near(list[0].getNormalized(), 0.5));
    check(list[0].valueToText(1.5f) == "1.50 units");
    check(list[0].textToValue("0.75") == 0.75f);

    // Mode: an enum's steps, whole numbers.
    check(list[1].numSteps() == 3);
    check(list[1].isDiscrete());
    list[1].setValue(1.6f);
    check(list[1].getValue() == 2.f);

    // Meter: the plugin's to write; a host edit is dropped.
    check(!list[2].isAutomatable());
    auto flushes = stub.flushes;
    list[2].setValue(1.f);
    check(list[2].getValue() == 0.f);
    check(stub.flushes == flushes);

    // Inactive, the Mode edit went out through a flush on the main thread.
    check(stub.mode == 2.0);
    Stub::checkClean(stub);
};

auto tGestures =
    test("HostedCLAP/gesturesFromABlockReachTheListenerOnTheMainThread") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);
    auto block = Block(*plugin);

    stub.emitInProcess = {Stub::gesture(Stub::gainId, true),
                          Stub::paramValue(Stub::gainId, 1.5),
                          Stub::gesture(Stub::gainId, false),
                          Stub::paramValue(Stub::meterId, 0.5),
                          Stub::paramValue(Stub::modeId, 3.0)};
    block.run();

    // Kept at once, the listener told on the main thread.
    check(plugin->parameters()[0].getValue() == 1.5f);
    check(plugin->parameters()[1].getValue() == 3.f);
    check(plugin->parameters()[2].getValue() == 0.5f);
    check(listener.events.empty());

    plugin->syncEditorWithProcessor();

    // Only the gesture is told. A value outside one, Mode's as much as the
    // read-only Meter's, is the plugin's output, kept and nothing more, as a
    // hosted VST3 processor's output is.
    check(eventsAre(listener.events, {"begin 0", "perform 0", "end 0"}));
    check(near(listener.lastValue, 0.75));
    check(listener.valueChanges == 0);

    plugin->setHostEditListener(nullptr);
    Stub::checkClean(stub);
};

auto tFlush = test("HostedCLAP/aFlushRequestedWhileInactiveRunsOnTheMainThread") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);

    // What a plugin's own GUI does while it is not active.
    stub.emitInFlush = {Stub::gesture(Stub::gainId, true),
                        Stub::paramValue(Stub::gainId, 0.5),
                        Stub::gesture(Stub::gainId, false)};
    const auto* params = stub.hostExtension<clap_host_params>(CLAP_EXT_PARAMS);
    params->request_flush(stub.host);

    check(stub.flushes == 0);
    plugin->syncEditorWithProcessor();

    check(stub.flushes == 1);
    check(plugin->parameters()[0].getValue() == 0.5f);
    check(listener.events.size() == 3);
    check(near(listener.lastValue, 0.25));

    // Posted to the loop too; once served, there is nothing left to run.
    eacp::Threads::runEventLoopFor(eacp::Time::MS {20});
    check(stub.flushes == 1);

    plugin->setHostEditListener(nullptr);
    Stub::checkClean(stub);
};

auto tDialect = test("HostedCLAP/notesGoInTheDialectThePortPrefersSortedByTime") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto block = Block(*plugin);
    block.run();
    stub.received.clear();

    // An edit queued from the message thread, then MIDI out of time order.
    plugin->parameters()[0].setValue(0.5f);

    const auto bytes = std::array<uint8_t, 4> {0xf0, 0x01, 0x02, 0xf7};
    auto& in = block.context.midiIn[0];
    in.add(MIDI::Event::noteOff(3, 60, 0.25f, 12));
    in.add(MIDI::Event::noteOn(3, 60, 0.5f, 7));
    in.add(MIDI::Event::polyAftertouch(3, 60, 0.25f, 9));
    in.add(MIDI::Event::controlChange(2, 1, 1.f, 2));
    in.add(MIDI::Event::sysEx(bytes.data(), static_cast<int>(bytes.size()), 4));
    in.add(MIDI::Event::noteOn(0, 61, 1.f, blockSize + 10));

    stub.emitInProcess = {Stub::noteOn(2, 64, 0.25, 5)};
    block.run();
    in.clear();

    auto types = std::vector<uint16_t>();

    for (const auto& event: stub.received)
        types.push_back(event.header.type);

    // Notes and pressure as CLAP events, the rest as MIDI; one past the block
    // lands on its last sample.
    check(types
          == std::vector<uint16_t> {CLAP_EVENT_PARAM_VALUE,
                                    CLAP_EVENT_MIDI,
                                    CLAP_EVENT_MIDI_SYSEX,
                                    CLAP_EVENT_NOTE_ON,
                                    CLAP_EVENT_NOTE_EXPRESSION,
                                    CLAP_EVENT_NOTE_OFF,
                                    CLAP_EVENT_NOTE_ON});

    if (const auto* on = stub.receivedOfType(CLAP_EVENT_NOTE_ON))
    {
        check(on->note.header.time == 7);
        check(on->note.channel == 3 && on->note.key == 60);
        check(on->note.port_index == 0 && on->note.note_id == -1);
        check(near(on->note.velocity, 0.5, 0.01));
    }

    if (const auto* pressure = stub.receivedOfType(CLAP_EVENT_NOTE_EXPRESSION))
    {
        check(pressure->expression.expression_id == CLAP_NOTE_EXPRESSION_PRESSURE);
        check(near(pressure->expression.value, 0.25, 0.01));
    }

    if (const auto* sysEx = stub.receivedOfType(CLAP_EVENT_MIDI_SYSEX))
        check(sysEx->sysEx.size == 4);

    if (!stub.received.empty())
        check(stub.received.back().header.time == blockSize - 1);

    check(stub.gain == 0.5);

    // The plugin's own note, out as MIDI.
    check(block.context.midiOut[0].size() == 1);

    if (block.context.midiOut[0].size() == 1)
    {
        const auto& out = block.context.midiOut[0][0];
        check(out.asNoteOn() != nullptr && out.asNoteOn()->pitch == 64);
        check(out.channel == 2 && out.sampleOffset == 5);
    }

    Stub::checkClean(stub);
};

auto tTransport = test("HostedCLAP/theTransportCarriesThePlayhead") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto block = Block(*plugin);

    block.context.playhead.isValid = true;
    block.context.playhead.isPlaying = true;
    block.context.playhead.isLooping = true;
    block.context.playhead.bpm = 90.0;
    block.context.playhead.ppqPosition = 6.5;
    block.context.playhead.barStartPpq = 4.0;
    block.context.playhead.sampleTime = 96000;
    block.context.playhead.loopStartPpq = 4.0;
    block.context.playhead.loopEndPpq = 8.0;
    block.context.playhead.timeSignature = {3, 4};
    block.run();

    const auto& transport = stub.lastTransport;
    auto beats = [](clap_beattime value)
    { return static_cast<double>(value) / CLAP_BEATTIME_FACTOR; };

    check(transport.header.type == CLAP_EVENT_TRANSPORT);
    check((transport.flags & CLAP_TRANSPORT_IS_PLAYING) != 0);
    check((transport.flags & CLAP_TRANSPORT_IS_LOOP_ACTIVE) != 0);
    check((transport.flags & CLAP_TRANSPORT_IS_RECORDING) == 0);
    check(transport.tempo == 90.0);
    check(near(beats(transport.song_pos_beats), 6.5));
    check(near(beats(transport.bar_start), 4.0));
    check(near(beats(transport.loop_end_beats), 8.0));
    check(near(static_cast<double>(transport.song_pos_seconds) / CLAP_SECTIME_FACTOR,
               2.0));
    check(transport.tsig_num == 3 && transport.tsig_denom == 4);
    check(transport.bar_number == 1);

    Stub::checkClean(stub);
};

auto tAudioThreadRequests =
    test("HostedCLAP/requestsFromTheAudioThreadRunOnTheMain") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);
    auto block = Block(*plugin);
    block.run();

    // A callback asked from a block runs on the main thread, once.
    stub.callbackFromProcess = true;
    block.run();
    check(stub.mainThreadCalls == 0);
    plugin->syncEditorWithProcessor();
    check(stub.mainThreadCalls == 1);
    plugin->syncEditorWithProcessor();
    check(stub.mainThreadCalls == 1);

    // A restart asked from a block: stopped on the audio thread's behalf,
    // deactivated, activated again, and the new latency read and told.
    stub.latencySamples = 100;
    stub.restartFromProcess = true;
    block.run();
    stub.calls.clear();
    plugin->syncEditorWithProcessor();

    check(stub.lifecycle() == "stop deactivate activate");
    check(plugin->takeLatencyChanged());
    check(plugin->latencySamples() == 100);
    check(listener.latencies == 1);

    block.run();
    check(stub.lifecycle() == "stop deactivate activate start");

    plugin->setHostEditListener(nullptr);
    Stub::checkClean(stub);
};

auto tTimers = test("HostedCLAP/timersTickOnTheMainThreadUntilUnregistered") = []
{
    auto stub = Stub::Plugin();
    stub.wantTimer = true;

    {
        auto plugin = Stub::host(stub);
        check(stub.timerRegistered);

        // It unregisters itself from inside its second tick.
        check(eacp::Threads::runEventLoopUntil([&stub] { return stub.ticks >= 2; },
                                               eacp::Time::MS {3000}));
        eacp::Threads::runEventLoopFor(eacp::Time::MS {50});
        check(stub.ticks == 2);
    }

    Stub::checkClean(stub);
};

auto tGuarded = test("HostedCLAP/nothingReachesAPluginOnceItIsDestroyed") = []
{
    auto stub = Stub::Plugin();
    stub.wantTimer = true;

    {
        auto plugin = Stub::host(stub);

        // Asked on the main thread, so posted to the loop, then torn down before
        // the loop runs it; the timer too stays registered.
        stub.host->request_callback(stub.host);
    }

    eacp::Threads::runEventLoopFor(eacp::Time::MS {50});

    check(stub.destroyed);
    check(stub.mainThreadCalls == 0);
    check(stub.ticks == 0);
    Stub::checkClean(stub);
};

auto tStubState = test("HostedCLAP/theStateGoesThroughClapState") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);

    stub.gain = 1.25;
    auto saved = plugin->saveState(StateContext::Session);
    check(saved == "gain=" + std::to_string(1.25));

    stub.gain = 0.0;
    plugin->loadState(saved, StateContext::Session);
    check(stub.gain == 1.25);
    check(plugin->parameters()[0].getValue() == 1.25f);

    // The plugin marking itself dirty reaches the listener.
    stub.hostExtension<clap_host_state>(CLAP_EXT_STATE)->mark_dirty(stub.host);
    check(listener.stateChanges == 1);

    plugin->setHostEditListener(nullptr);
    Stub::checkClean(stub);
};

// ------------------------------------------------- a module, shared per format

auto tSharedModule = test("HostedCLAP/instancesOfAModuleShareOneEntryInit") = []
{
    const auto path = fixture("HostFixtureEffect");
    const auto format = CLAPFormat();
    const auto scan = format.scan(path);
    check(scan.classes.size() == 1);

    if (scan.classes.empty())
        return;

    auto first = format.load(scan.classes[0], {.name = "Hosting Tests"});
    auto second = format.load(scan.classes[0], {.name = "Hosting Tests"});
    auto* a = dynamic_cast<CLAPHostedPlugin*>(first.plugin.get());
    auto* b = dynamic_cast<CLAPHostedPlugin*>(second.plugin.get());
    check(a != nullptr && b != nullptr);

    if (a == nullptr || b == nullptr)
        return;

    // One module, its entry init()ed once, for both instances and for a scan
    // or a lookup through the same format meanwhile.
    check(a->clapModule() != nullptr);
    check(a->clapModule() == b->clapModule());

    auto error = std::string();
    check(format.openModule(path, error) == a->clapModule());
    check(format.scan(path).classes.size() == 1);

    // Another format is another cache: its own init(), as the spec allows.
    const auto other = CLAPFormat();
    check(other.openModule(path, error) != a->clapModule());

    // The first going leaves the entry init()ed under the second, which still
    // processes.
    auto module = std::weak_ptr<CLAPModule>(a->clapModule());
    first.plugin.reset();
    check(!module.expired());

    auto block = Block(*second.plugin);
    block.fillInput(0, 0.5f);
    block.run();
    check(near(block.context.outputs[0][0][0], 0.5));

    // deinit() once the last instance is gone; a later load starts afresh.
    second.plugin.reset();
    check(module.expired());

    auto again = format.load(scan.classes[0], {.name = "Hosting Tests"});
    check(again.plugin != nullptr);
};

// ----------------------------------------------- the stub: edits and flushes

auto tIdleFlush =
    test("HostedCLAP/aFlushAndEditsWhileActiveButIdleGoOutAsTheAudioThread") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);
    auto block = Block(*plugin);
    block.run();

    // The plugin's GUI edits while the host runs no blocks (its transport
    // stopped): no block takes the request, so a flush must.
    stub.emitInFlush = {Stub::gesture(Stub::gainId, true),
                        Stub::paramValue(Stub::gainId, 0.5),
                        Stub::gesture(Stub::gainId, false)};
    stub.hostExtension<clap_host_params>(CLAP_EXT_PARAMS)->request_flush(stub.host);

    // A block ran just now, so it is not idle yet.
    plugin->syncEditorWithProcessor();
    check(stub.flushes == 0);

    check(eacp::Threads::runEventLoopUntil([&stub] { return stub.flushes == 1; },
                                           eacp::Time::MS {3000}));
    plugin->syncEditorWithProcessor();

    check(plugin->isActive());
    check(plugin->parameters()[0].getValue() == 0.5f);
    check(eventsAre(listener.events, {"begin 0", "perform 0", "end 0"}));
    check(near(listener.lastValue, 0.25));

    // A host edit, with still no block, reaches it the same way.
    plugin->parameters()[1].setValue(2.f);
    check(eacp::Threads::runEventLoopUntil([&stub] { return stub.mode == 2.0; },
                                           eacp::Time::MS {3000}));
    check(stub.flushes == 2);

    // And blocks run on afterwards, still processing.
    auto blocks = stub.blocks;
    block.run();
    check(stub.blocks == blocks + 1);
    check(block.context.outputs[0][0][0] == static_cast<float>(stub.gain));
    check(stub.lifecycle() == "init activate start");

    plugin->setHostEditListener(nullptr);
    Stub::checkClean(stub);
};

auto tOverflow = test("HostedCLAP/editsPastAFullQueueAreKeptAtTheirLatestValue") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto block = Block(*plugin);
    block.run();

    auto& gain = plugin->parameters()[0];

    // More edits between two blocks than the queue holds: the last one wins.
    for (auto i = 0; i < 1500; ++i)
        gain.setValue(static_cast<float>(i % 100) / 100.f);

    gain.setValue(1.75f);

    block.run();
    check(stub.gain != 1.75);

    plugin->syncEditorWithProcessor();
    block.run();
    check(stub.gain == 1.75);

    // The same with no block at all: the idle flush takes them.
    for (auto i = 0; i < 1500; ++i)
        gain.setValue(static_cast<float>(i % 100) / 100.f);

    gain.setValue(0.25f);
    check(eacp::Threads::runEventLoopUntil([&stub] { return stub.gain == 0.25; },
                                           eacp::Time::MS {3000}));

    Stub::checkClean(stub);
};

auto tStaleEdit = test("HostedCLAP/aStateLoadDiscardsEditsStillQueued") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto block = Block(*plugin);
    block.run();

    // Queued for a block, then a preset loaded before it ran.
    plugin->parameters()[0].setValue(0.5f);
    plugin->loadState("gain=1.25", StateContext::Preset);

    block.run();
    check(stub.gain == 1.25);
    check(plugin->parameters()[0].getValue() == 1.25f);

    // An edit made after the load goes through.
    plugin->parameters()[0].setValue(0.75f);
    block.run();
    check(stub.gain == 0.75);

    Stub::checkClean(stub);
};

auto tAudioRewrite = test("HostedCLAP/aSecondAudioThreadWriteReplacesTheFirst") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto block = Block(*plugin);
    block.run();
    stub.received.clear();

    auto values = [&stub]
    {
        auto count = 0;

        for (const auto& event: stub.received)
            count += event.header.type == CLAP_EVENT_PARAM_VALUE ? 1 : 0;

        return count;
    };

    check(plugin->setParameterFromAudioThread(0, 0.25f));
    check(plugin->setParameterFromAudioThread(1, 1.f));
    check(plugin->setParameterFromAudioThread(0, 0.75f));
    block.run();

    check(values() == 2);
    check(stub.gain == 1.5);
    check(stub.mode == 3.0);

    // The next block's writes are its own.
    stub.received.clear();
    check(plugin->setParameterFromAudioThread(0, 0.5f));
    block.run();
    check(values() == 1);
    check(stub.gain == 1.0);

    Stub::checkClean(stub);
};

auto tMidiFull = test("HostedCLAP/midiPastTheListsRoomIsDroppedWhole") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto block = Block(*plugin);
    block.run();
    stub.received.clear();

    for (auto i = 0; i < ProcessContext::defaultMidiCapacity * 3; ++i)
        block.context.midiIn[0].add(MIDI::Event::noteOn(0, 60, 0.5f, i % blockSize));

    block.run();
    block.context.midiIn[0].clear();

    // As many as fit, every one a whole note.
    check(static_cast<int>(stub.received.size())
          >= ProcessContext::defaultMidiCapacity);
    check(static_cast<int>(stub.received.size())
          < ProcessContext::defaultMidiCapacity * 3);

    for (const auto& event: stub.received)
        check(event.header.type == CLAP_EVENT_NOTE_ON && event.note.key == 60);

    Stub::checkClean(stub);
};

// ------------------------------------------- the stub: the plugin's own edits

auto tFullGestures = test("HostedCLAP/aFullGestureQueueDropsWholeGestures") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);
    auto block = Block(*plugin);

    // Whole Gain gestures, three events each.
    auto gainGestures = [](int count)
    {
        auto events = std::vector<CLAPEvent>();

        for (auto i = 0; i < count; ++i)
        {
            events.push_back(Stub::gesture(Stub::gainId, true));
            events.push_back(Stub::paramValue(Stub::gainId, 0.01 * (i % 100)));
            events.push_back(Stub::gesture(Stub::gainId, false));
        }

        return events;
    };

    auto append = [](std::vector<CLAPEvent>& to, std::vector<CLAPEvent> more)
    { to.insert(to.end(), more.begin(), more.end()); };

    // Over as many blocks as the output list needs, with no drain between them:
    // a gesture open at a block's end stays open into the next.
    auto runInBlocks = [&](const std::vector<CLAPEvent>& events)
    {
        constexpr auto perBlock = size_t {300};

        for (auto from = size_t {0}; from < events.size(); from += perBlock)
        {
            auto to = std::min(events.size(), from + perBlock);
            stub.emitInProcess.assign(events.begin() + static_cast<long>(from),
                                      events.begin() + static_cast<long>(to));
            block.run();
        }
    };

    // The queue (1024) all but full of whole gestures: the Mode gesture after
    // them finds no room for its begin and is dropped whole, its value and end
    // with it.
    auto events = gainGestures(341);
    append(events,
           {Stub::gesture(Stub::modeId, true),
            Stub::paramValue(Stub::modeId, 1.0),
            Stub::gesture(Stub::modeId, false)});
    runInBlocks(events);
    plugin->syncEditorWithProcessor();

    check(gesturesBalanced(listener.events));
    check(countOf(listener.events, "begin 0") == 341);
    check(countOf(listener.events, "begin 1") == 0);
    check(countOf(listener.events, "end 1") == 0);
    check(plugin->parameters()[1].getValue() == 1.f);

    // A gesture begun first keeps the room for its end, however many gestures
    // follow it and are dropped.
    listener.events.clear();
    events = {Stub::gesture(Stub::modeId, true)};
    append(events, gainGestures(400));
    append(events,
           {Stub::paramValue(Stub::modeId, 2.0), Stub::gesture(Stub::modeId, false)});
    runInBlocks(events);
    plugin->syncEditorWithProcessor();

    check(gesturesBalanced(listener.events));
    check(countOf(listener.events, "begin 0") < 400);
    check(countOf(listener.events, "begin 1") == 1);
    check(countOf(listener.events, "end 1") == 1);
    check(plugin->parameters()[1].getValue() == 2.f);

    // Drained, there is room again for everything.
    listener.events.clear();
    stub.emitInProcess = {Stub::gesture(Stub::modeId, true),
                          Stub::paramValue(Stub::modeId, 3.0),
                          Stub::gesture(Stub::modeId, false)};
    block.run();
    plugin->syncEditorWithProcessor();
    check(eventsAre(listener.events, {"begin 1", "perform 1", "end 1"}));

    plugin->setHostEditListener(nullptr);
    Stub::checkClean(stub);
};

auto tReentrantListener =
    test("HostedCLAP/aListenerEditingFromItsCallbackLosesNoGestureEnd") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);

    // Inactive, so the listener's own edit is flushed at once, from inside the
    // dispatch of the plugin's gesture.
    listener.onBegin = [&plugin](int index)
    {
        if (index == 0)
            plugin->parameters()[1].setValue(2.f);
    };

    stub.emitInFlush = {Stub::gesture(Stub::gainId, true),
                        Stub::paramValue(Stub::gainId, 0.5),
                        Stub::gesture(Stub::gainId, false)};
    stub.hostExtension<clap_host_params>(CLAP_EXT_PARAMS)->request_flush(stub.host);
    plugin->syncEditorWithProcessor();

    check(eventsAre(listener.events, {"begin 0", "perform 0", "end 0"}));
    check(stub.mode == 2.0);
    check(stub.flushes == 2);

    // An edit made from inside the flush itself (a plugin callback reaching the
    // host, which edits) waits for the flush to return, then goes in another.
    listener.onBegin = nullptr;
    stub.duringFlush = [&plugin] { plugin->parameters()[1].setValue(1.f); };
    plugin->parameters()[0].setValue(0.25f);

    check(stub.gain == 0.25);
    check(stub.mode == 1.0);
    check(stub.flushes == 4);

    plugin->setHostEditListener(nullptr);
    Stub::checkClean(stub);
};

// ------------------------------------------------------ the stub: its ports

auto tFailedNotePort = test("HostedCLAP/aNotePortItWontDescribeKeepsItsIndex") = []
{
    auto stub = Stub::Plugin();
    stub.noteInputs = 2;
    stub.failNoteInput = 0;
    auto plugin = Stub::host(stub);

    check(plugin->getBusLayout().midiInputs.size() == 2);
    check(plugin->getBusLayout().midiInputs[1].name == "Notes In");

    auto block = Block(*plugin);
    block.run();
    stub.received.clear();

    // The first port takes nothing; the second's note names port 1.
    block.context.midiIn[0].add(MIDI::Event::noteOn(0, 50, 0.5f, 0));
    block.context.midiIn[1].add(MIDI::Event::noteOn(0, 62, 0.5f, 1));
    block.run();

    check(stub.received.size() == 1);

    if (const auto* on = stub.receivedOfType(CLAP_EVENT_NOTE_ON))
        check(on->note.port_index == 1 && on->note.key == 62);

    Stub::checkClean(stub);
};

auto tFailedAudioPort =
    test("HostedCLAP/anAudioPortItWontDescribeKeepsItFromProcessing") = []
{
    {
        auto stub = Stub::Plugin();
        stub.failAudioOutput = true;

        auto result = CLAPHostedPlugin::create({},
                                               nullptr,
                                               [&stub](const clap_host* clapHost)
                                               { return stub.make(clapHost); },
                                               {});

        check(result.plugin == nullptr);
        check(result.error.find("audio port") != std::string::npos);
        check(stub.lifecycle() == "init destroy");
        Stub::checkClean(stub);
    }

    // Refused later, the port's channels are unknown, so it is not activated.
    auto stub = Stub::Plugin();

    {
        auto plugin = Stub::host(stub);
        auto layout = plugin->getBusLayout();
        stub.failAudioOutput = true;

        plugin->prepare(
            {.sampleRate = 48000.0, .maxBlockSize = blockSize, .layout = layout});
        check(!plugin->isActive());
        check(plugin->getBusLayout().outputs.size() == 1);
    }

    check(stub.lifecycle() == "init destroy");
    Stub::checkClean(stub);
};

// ------------------------------------------------- the stub: thread checks

auto tTailThreads = test("HostedCLAP/theTailIsAskedOnTheMainOrAudioThread") = []
{
    auto stub = Stub::Plugin();
    auto plugin = Stub::host(stub);

    check(plugin->tailSamples() == 480);

    auto fromAnother = 0;
    auto thread = std::thread([&] { fromAnother = plugin->tailSamples(); });
    thread.join();
    check(fromAnother == 480);

    Stub::checkClean(stub);
};

auto tTimerChecks =
    test("HostedCLAP/aTimerUnregisteredOffTheMainThreadIsRefused") = []
{
    auto stub = Stub::Plugin();
    stub.wantTimer = true;
    auto plugin = Stub::host(stub);
    auto block = Block(*plugin);

    stub.unregisterTimerFromProcess = true;
    block.run();
    check(!stub.unregisteredFromProcess);

    // Still registered: it ticks.
    check(eacp::Threads::runEventLoopUntil([&stub] { return stub.ticks >= 1; },
                                           eacp::Time::MS {3000}));

    Stub::checkClean(stub);
};

auto tTimerModal =
    test("HostedCLAP/aTimerRetiredInsideItsTickOutlivesAServiceItRuns") = []
{
    auto stub = Stub::Plugin();
    stub.wantTimer = true;

    {
        auto plugin = Stub::host(stub);

        // Its first tick unregisters it, then runs a service, as a tick pumping
        // a modal loop would: the timer it is inside must not go yet.
        stub.duringTick = [&stub, &plugin]
        {
            stub.hostExtension<clap_host_timer_support>(CLAP_EXT_TIMER_SUPPORT)
                ->unregister_timer(stub.host, stub.timer);
            plugin->syncEditorWithProcessor();
        };

        check(eacp::Threads::runEventLoopUntil([&stub] { return stub.ticks >= 1; },
                                               eacp::Time::MS {3000}));
        eacp::Threads::runEventLoopFor(eacp::Time::MS {50});
        check(stub.ticks == 1);

        // Gone at the next service outside every tick.
        plugin->syncEditorWithProcessor();
    }

    Stub::checkClean(stub);
};
} // namespace
} // namespace HostedCLAPTests
