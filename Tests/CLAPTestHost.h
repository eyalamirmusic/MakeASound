#pragma once

// The CLAP adapter hosted in-process the way a DAW drives it, through the C API
// alone, shared by the CLAP suites: a clap_host that records what the plugin
// asks of it (flushes, rescans, dirty marks, restarts, callbacks, descriptors
// and timers registered, and the gui's resize, hint, show and hide requests),
// vector-backed
// input and output event lists, memory streams, and a fixture owning the host's
// buffers and process data. A plugin comes from an Adapter made over any
// plugin, as the factory makes one, or from the factory itself.

#include <MakeASound/Plugin/CLAP/Adapter.h>
#include <MakeASound/Plugin/CLAP/Factory.h>

#include <clap/clap.h>

#include "TestPlugins.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CLAPHost
{
using namespace MakeASound;

constexpr auto maxBlock = TestPlugins::blockSize;
constexpr auto maxEvents = 256;

// Records every request; the thread checks say yes to either thread, since a
// test is both.
struct TestHost
{
    TestHost()
    {
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "MakeASound Test Host";
        host.vendor = "MakeASound";
        host.url = "";
        host.version = "1.0";
        host.get_extension = getExtension;
        host.request_restart = [](const clap_host* h) { self(h).restarts++; };
        host.request_process = [](const clap_host* h) { self(h).processRequests++; };
        host.request_callback = [](const clap_host* h) { self(h).callbacks++; };

        params.rescan = [](const clap_host* h, clap_param_rescan_flags flags)
        { self(h).rescans.push_back(flags); };
        params.clear = [](const clap_host*, clap_id, clap_param_clear_flags) {};
        params.request_flush = [](const clap_host* h) { self(h).flushRequests++; };

        state.mark_dirty = [](const clap_host* h) { self(h).dirtyMarks++; };
        latency.changed = [](const clap_host* h) { self(h).latencyChanges++; };

        threadCheck.is_main_thread = [](const clap_host*) { return true; };
        threadCheck.is_audio_thread = [](const clap_host*) { return true; };

        trackInfo.get = [](const clap_host* h, clap_track_info* info)
        {
            if (!self(h).track)
                return false;

            *info = *self(h).track;
            return true;
        };

        posixFd.register_fd =
            [](const clap_host* h, int fd, clap_posix_fd_flags_t flags)
        {
            self(h).fds.push_back({fd, flags});
            return true;
        };
        posixFd.modify_fd = [](const clap_host*, int, clap_posix_fd_flags_t)
        { return true; };
        posixFd.unregister_fd = [](const clap_host* h, int fd)
        {
            auto& fds = self(h).fds;
            auto found =
                std::find_if(fds.begin(),
                             fds.end(),
                             [fd](const auto& entry) { return entry.first == fd; });

            if (found == fds.end())
                return false;

            fds.erase(found);
            return true;
        };

        timers.register_timer =
            [](const clap_host* h, uint32_t periodMs, clap_id* timerId)
        {
            auto& me = self(h);
            *timerId = me.nextTimer++;
            me.timerPeriods.push_back({*timerId, periodMs});
            return true;
        };
        timers.unregister_timer = [](const clap_host* h, clap_id timerId)
        {
            auto& periods = self(h).timerPeriods;
            auto found = std::find_if(periods.begin(),
                                      periods.end(),
                                      [timerId](const auto& entry)
                                      { return entry.first == timerId; });

            if (found == periods.end())
                return false;

            periods.erase(found);
            return true;
        };

        gui.resize_hints_changed = [](const clap_host* h)
        { self(h).resizeHintsChanges++; };
        gui.request_resize = [](const clap_host* h, uint32_t width, uint32_t height)
        {
            auto& me = self(h);
            me.resizeRequests.push_back({width, height});

            if (me.onResizeRequest)
                me.onResizeRequest(width, height);

            return me.acceptResize;
        };
        gui.request_show = [](const clap_host* h)
        {
            self(h).showRequests++;
            return true;
        };
        gui.request_hide = [](const clap_host* h)
        {
            self(h).hideRequests++;
            return true;
        };
        gui.closed = [](const clap_host* h, bool wasDestroyed)
        { self(h).closings.push_back(wasDestroyed); };
    }

    TestHost(const TestHost&) = delete;
    TestHost& operator=(const TestHost&) = delete;

    static TestHost& self(const clap_host* h)
    {
        return *static_cast<TestHost*>(h->host_data);
    }

    static const void* getExtension(const clap_host* h, const char* id)
    {
        auto& me = self(h);

        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &me.params;

        if (std::strcmp(id, CLAP_EXT_STATE) == 0)
            return &me.state;

        if (std::strcmp(id, CLAP_EXT_LATENCY) == 0)
            return &me.latency;

        if (std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0)
            return &me.threadCheck;

        if (std::strcmp(id, CLAP_EXT_TRACK_INFO) == 0)
            return &me.trackInfo;

        if (std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT) == 0 && me.offersPosixFd)
            return &me.posixFd;

        if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0)
            return &me.timers;

        if (std::strcmp(id, CLAP_EXT_GUI) == 0 && me.offersGui)
            return &me.gui;

        return nullptr;
    }

    int rescansWith(uint32_t flag) const
    {
        return static_cast<int>(std::count_if(rescans.begin(),
                                              rescans.end(),
                                              [flag](auto flags)
                                              { return (flags & flag) != 0; }));
    }

    void clearRecords()
    {
        flushRequests = restarts = processRequests = callbacks = 0;
        dirtyMarks = latencyChanges = 0;
        rescans.clear();
        resizeHintsChanges = showRequests = hideRequests = 0;
        resizeRequests.clear();
        closings.clear();
    }

    clap_host host {};
    clap_host_params params {};
    clap_host_state state {};
    clap_host_latency latency {};
    clap_host_thread_check threadCheck {};
    clap_host_track_info trackInfo {};
    clap_host_posix_fd_support posixFd {};
    clap_host_timer_support timers {};
    clap_host_gui gui {};

    // What track_info.get answers; nothing while empty.
    std::optional<clap_track_info> track;

    // The descriptors and timers registered now, each with its flags or period;
    // without posix-fd-support only the timers are offered.
    bool offersPosixFd = true;
    std::vector<std::pair<int, clap_posix_fd_flags_t>> fds;
    std::vector<std::pair<clap_id, uint32_t>> timerPeriods;
    clap_id nextTimer = 1;

    int flushRequests = 0;
    int restarts = 0;
    int processRequests = 0;
    int callbacks = 0;
    int dirtyMarks = 0;
    int latencyChanges = 0;
    std::vector<uint32_t> rescans;

    // clap.gui's host side, offered unless turned off before the plugin's init:
    // each request_resize's size, answered with acceptResize after
    // onResizeRequest ran (where a host would call set_size back), and the
    // resize-hint changes, show and hide requests and closings.
    bool offersGui = true;
    bool acceptResize = true;
    std::function<void(uint32_t, uint32_t)> onResizeRequest;
    std::vector<std::pair<uint32_t, uint32_t>> resizeRequests;
    int resizeHintsChanges = 0;
    int showRequests = 0;
    int hideRequests = 0;
    std::vector<bool> closings;
};

// One event of any core type, as an event list stores it.
union AnyEvent
{
    clap_event_header header;
    clap_event_note note;
    clap_event_param_value param;
    clap_event_param_gesture gesture;
    clap_event_transport transport;
    clap_event_midi midi;
    clap_event_midi_sysex sysEx;
};

inline clap_event_header headerOf(uint32_t size, uint16_t type, uint32_t time)
{
    return {size, time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
}

// The host's events for a block, in the order added (a host sorts them).
struct InputEvents
{
    InputEvents()
    {
        events.reserve(maxEvents);
        list.ctx = this;
        list.size = [](const clap_input_events* l)
        { return static_cast<uint32_t>(of(l).events.size()); };
        list.get = [](const clap_input_events* l, uint32_t index)
        {
            const auto& events = of(l).events;
            return index < events.size() ? &events[index].header : nullptr;
        };
    }

    InputEvents(const InputEvents&) = delete;
    InputEvents& operator=(const InputEvents&) = delete;

    static InputEvents& of(const clap_input_events* l)
    {
        return *static_cast<InputEvents*>(l->ctx);
    }

    void add(const clap_event_header& header)
    {
        auto event = AnyEvent {};
        std::memcpy(
            &event, &header, std::min<size_t>(header.size, sizeof(AnyEvent)));
        events.push_back(event);
    }

    void param(clap_id id, uint32_t time, double value)
    {
        auto event = clap_event_param_value {};
        event.header =
            headerOf(sizeof(clap_event_param_value), CLAP_EVENT_PARAM_VALUE, time);
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        add(event.header);
    }

    void note(uint16_t type,
              uint32_t time,
              int16_t key,
              double velocity = 0.8,
              int16_t channel = 0,
              int16_t port = 0)
    {
        auto event = clap_event_note {};
        event.header = headerOf(sizeof(clap_event_note), type, time);
        event.note_id = -1;
        event.port_index = port;
        event.channel = channel;
        event.key = key;
        event.velocity = velocity;
        add(event.header);
    }

    void midi(uint32_t time, uint8_t status, uint8_t data1, uint8_t data2)
    {
        auto event = clap_event_midi {};
        event.header = headerOf(sizeof(clap_event_midi), CLAP_EVENT_MIDI, time);
        event.port_index = 0;
        event.data[0] = status;
        event.data[1] = data1;
        event.data[2] = data2;
        add(event.header);
    }

    // The bytes must outlive the block.
    void sysEx(uint32_t time, const uint8_t* bytes, uint32_t size)
    {
        auto event = clap_event_midi_sysex {};
        event.header =
            headerOf(sizeof(clap_event_midi_sysex), CLAP_EVENT_MIDI_SYSEX, time);
        event.port_index = 0;
        event.buffer = bytes;
        event.size = size;
        add(event.header);
    }

    void clear() { events.clear(); }

    std::vector<AnyEvent> events;
    clap_input_events list {};
};

// What the plugin sends, up to its reserved capacity: past it, try_push
// refuses, so the host never allocates.
struct OutputEvents
{
    OutputEvents()
    {
        events.reserve(maxEvents);
        list.ctx = this;
        list.try_push =
            [](const clap_output_events* l, const clap_event_header* event)
        {
            auto& me = *static_cast<OutputEvents*>(l->ctx);

            if (event == nullptr || me.events.size() >= me.events.capacity()
                || me.events.size() >= me.limit || event->size > sizeof(AnyEvent))
                return false;

            auto copy = AnyEvent {};
            std::memcpy(&copy, event, event->size);
            me.events.push_back(copy);
            return true;
        };
    }

    OutputEvents(const OutputEvents&) = delete;
    OutputEvents& operator=(const OutputEvents&) = delete;

    std::vector<const AnyEvent*> ofType(uint16_t type) const
    {
        auto found = std::vector<const AnyEvent*> {};

        for (const auto& event: events)
            if (event.header.type == type)
                found.push_back(&event);

        return found;
    }

    void clear() { events.clear(); }

    std::vector<AnyEvent> events;
    clap_output_events list {};

    // A host whose list takes fewer events refuses past this many.
    size_t limit = maxEvents;
};

struct OutStream
{
    OutStream()
    {
        stream.ctx = this;
        stream.write = [](const clap_ostream* s, const void* buffer, uint64_t size)
        {
            auto& me = *static_cast<OutStream*>(s->ctx);
            auto count = std::min<uint64_t>(size, me.maxWrite);
            me.data.append(static_cast<const char*>(buffer), count);
            return static_cast<int64_t>(count);
        };
    }

    OutStream(const OutStream&) = delete;
    OutStream& operator=(const OutStream&) = delete;

    clap_ostream stream {};
    std::string data;

    // A host may take fewer bytes than offered.
    uint64_t maxWrite = 1u << 20;
};

struct InStream
{
    explicit InStream(std::string dataToUse, uint64_t maxReadToUse = 1u << 20)
        : data(std::move(dataToUse))
        , maxRead(maxReadToUse)
    {
        stream.ctx = this;
        stream.read = [](const clap_istream* s, void* buffer, uint64_t size)
        {
            auto& me = *static_cast<InStream*>(s->ctx);
            auto left = static_cast<uint64_t>(me.data.size() - me.position);
            auto count = std::min({size, left, me.maxRead});
            std::memcpy(buffer, me.data.data() + me.position, count);
            me.position += static_cast<size_t>(count);
            return static_cast<int64_t>(count);
        };
    }

    InStream(const InStream&) = delete;
    InStream& operator=(const InStream&) = delete;

    clap_istream stream {};
    std::string data;
    size_t position = 0;
    uint64_t maxRead;
};

// One bus of host memory: separate channel storage and the pointer table.
struct HostBus
{
    explicit HostBus(int numChannels)
        : storage(static_cast<size_t>(numChannels), std::vector<float>(maxBlock))
    {
        for (auto& channel: storage)
            pointers.push_back(channel.data());
    }

    std::vector<std::vector<float>> storage;
    std::vector<float*> pointers;
};

inline const clap_plugin_descriptor& testDescriptor()
{
    static const char* const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                           nullptr};
    static const auto descriptor = clap_plugin_descriptor {CLAP_VERSION,
                                                           "com.makeasound.tests",
                                                           "Test",
                                                           "MakeASound Tests",
                                                           "",
                                                           "",
                                                           "",
                                                           "1.0.0",
                                                           "",
                                                           features};
    return descriptor;
}

// A plugin initialised, activated at 48 kHz in blocks of up to 64 and
// processing, with the host's side of a block ready to fill. Destroyed through
// its own destroy(), as a host does.
template <typename P>
struct PluginHost
{
    explicit PluginHost(ParameterIdentity identity = {})
        : PluginHost(OwningPointer<Plugin>(EA::makeOwned<P>()), identity)
    {
    }

    // Over a plugin made elsewhere, as the factory makes one.
    PluginHost(OwningPointer<Plugin> plugin, ParameterIdentity identity)
    {
        auto* adapter = new CLAP::Adapter(
            &host.host, &testDescriptor(), std::move(plugin), identity);
        start(adapter->clapPlugin());
    }

    // Over what a factory created against `host.host`, through a callback
    // given the host before the plugin exists.
    template <typename CreateFn>
    explicit PluginHost(CreateFn&& create, int)
    {
        start(create(&host.host));
    }

    ~PluginHost()
    {
        if (clap == nullptr)
            return;

        if (processing)
            clap->stop_processing(clap);

        if (active)
            clap->deactivate(clap);

        clap->destroy(clap);
    }

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    void start(const clap_plugin* created)
    {
        clap = created;

        if (clap == nullptr)
            return;

        initialised = clap->init(clap);
        params = extension<clap_plugin_params>(CLAP_EXT_PARAMS);
        state = extension<clap_plugin_state>(CLAP_EXT_STATE);
        stateContext = extension<clap_plugin_state_context>(CLAP_EXT_STATE_CONTEXT);
        audioPorts = extension<clap_plugin_audio_ports>(CLAP_EXT_AUDIO_PORTS);
        notePorts = extension<clap_plugin_note_ports>(CLAP_EXT_NOTE_PORTS);
        latency = extension<clap_plugin_latency>(CLAP_EXT_LATENCY);
        tail = extension<clap_plugin_tail>(CLAP_EXT_TAIL);
        render = extension<clap_plugin_render>(CLAP_EXT_RENDER);
        trackInfo = extension<clap_plugin_track_info>(CLAP_EXT_TRACK_INFO);
        gui = extension<clap_plugin_gui>(CLAP_EXT_GUI);

        configure(48000.0, maxBlock);

        const auto& layout = adapter().wrapper().busLayout();

        for (const auto& bus: layout.inputs)
            inputs.emplace_back(bus.numChannels);

        for (const auto& bus: layout.outputs)
            outputs.emplace_back(bus.numChannels);

        inputBuffers.resize(inputs.size());
        outputBuffers.resize(outputs.size());
    }

    template <typename Extension>
    const Extension* extension(const char* id) const
    {
        return static_cast<const Extension*>(clap->get_extension(clap, id));
    }

    // Deactivated and activated again, processing from then on.
    bool configure(double sampleRate, int maxFrames)
    {
        stop();

        active =
            clap->activate(clap, sampleRate, 1, static_cast<uint32_t>(maxFrames));

        if (active)
            processing = clap->start_processing(clap);

        return active;
    }

    void stop()
    {
        if (processing)
            clap->stop_processing(clap);

        if (active)
            clap->deactivate(clap);

        processing = false;
        active = false;
    }

    CLAP::Adapter& adapter() const { return *CLAP::Adapter::from(clap); }
    P& plugin() const { return static_cast<P&>(adapter().plugin()); }

    clap_id idOf(const Parameter& param) const
    {
        const auto& list = adapter().plugin().parameters();
        return list.entry(list.indexOf(&param)).clapId;
    }

    void clearEvents()
    {
        in.clear();
        out.clear();
    }

    // Points the process data at the host's buffers for a block; in place, each
    // output bus reuses the input bus of its index.
    void fillBlock(int numSamples, bool inPlace = false)
    {
        for (auto i = size_t {0}; i < inputs.size(); ++i)
        {
            inputBuffers[i] = {};
            inputBuffers[i].channel_count =
                static_cast<uint32_t>(inputs[i].pointers.size());
            inputBuffers[i].data32 = inputs[i].pointers.data();
        }

        for (auto i = size_t {0}; i < outputs.size(); ++i)
        {
            auto& source = inPlace && i < inputs.size() ? inputs[i] : outputs[i];
            outputBuffers[i] = {};
            outputBuffers[i].channel_count =
                static_cast<uint32_t>(source.pointers.size());
            outputBuffers[i].data32 = source.pointers.data();
            outputBuffers[i].constant_mask = ~uint64_t {0};
        }

        data = {};
        data.steady_time = -1;
        data.frames_count = static_cast<uint32_t>(numSamples);
        data.transport = useTransport ? &transport : nullptr;
        data.audio_inputs = inputBuffers.empty() ? nullptr : inputBuffers.data();
        data.audio_outputs = outputBuffers.empty() ? nullptr : outputBuffers.data();
        data.audio_inputs_count = static_cast<uint32_t>(inputBuffers.size());
        data.audio_outputs_count = static_cast<uint32_t>(outputBuffers.size());
        data.in_events = &in.list;
        data.out_events = &out.list;
    }

    clap_process_status run() { return clap->process(clap, &data); }

    // params.flush with the input events, the output to `out`.
    void flush() { params->flush(clap, &in.list, &out.list); }

    double value(clap_id id) const
    {
        auto result = 0.0;
        params->get_value(clap, id, &result);
        return result;
    }

    clap_param_info info(int position) const
    {
        auto result = clap_param_info {};
        params->get_info(clap, static_cast<uint32_t>(position), &result);
        return result;
    }

    // The info of the parameter with this clap_id; id 0 and no flags when none.
    clap_param_info infoOf(clap_id id) const
    {
        for (auto i = 0u; i < params->count(clap); ++i)
            if (auto found = info(static_cast<int>(i)); found.id == id)
                return found;

        return {};
    }

    std::string save(std::optional<uint32_t> context = {})
    {
        auto stream = OutStream {};

        if (context)
            stateContext->save(clap, &stream.stream, *context);
        else
            state->save(clap, &stream.stream);

        return stream.data;
    }

    bool load(std::string document, std::optional<uint32_t> context = {})
    {
        auto stream = InStream {std::move(document)};

        if (context)
            return stateContext->load(clap, &stream.stream, *context);

        return state->load(clap, &stream.stream);
    }

    TestHost host;
    const clap_plugin* clap = nullptr;
    bool initialised = false;
    bool active = false;
    bool processing = false;

    const clap_plugin_params* params = nullptr;
    const clap_plugin_state* state = nullptr;
    const clap_plugin_state_context* stateContext = nullptr;
    const clap_plugin_audio_ports* audioPorts = nullptr;
    const clap_plugin_note_ports* notePorts = nullptr;
    const clap_plugin_latency* latency = nullptr;
    const clap_plugin_tail* tail = nullptr;
    const clap_plugin_render* render = nullptr;
    const clap_plugin_track_info* trackInfo = nullptr;
    const clap_plugin_gui* gui = nullptr;

    std::vector<HostBus> inputs;
    std::vector<HostBus> outputs;
    std::vector<clap_audio_buffer> inputBuffers;
    std::vector<clap_audio_buffer> outputBuffers;

    InputEvents in;
    OutputEvents out;
    clap_event_transport transport {};
    bool useTransport = false;
    clap_process data {};
};

// An Adapter over a new instance of `description`'s plugin, made as the
// module's factory makes one: the identity it reads from the description,
// created under the CLAP format.
inline PluginHost<Plugin> hostFor(const PluginDescription& description)
{
    setCurrentPluginFormat(PluginFormat::CLAP);

    return PluginHost<Plugin>(description.create(),
                              parameterIdentityFor(description));
}

} // namespace CLAPHost
