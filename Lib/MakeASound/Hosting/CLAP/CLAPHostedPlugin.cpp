#include "CLAPHostedPlugin.h"
#include "CLAPModule.h"
#include "CLAPStreams.h"

#include <clap/fixedpoint.h>
#include <clap/version.h>

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Threads/ThreadUtils.h>
#include <eacp/Core/Threads/Timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <thread>

namespace MakeASound::Hosting
{

namespace
{
// Room in the input list past what a block can carry (one write per parameter
// from the audio thread, the edit queue, the MIDI), and in the output list past
// a gesture and a value per parameter plus the MIDI, for a plugin sending more
// than a block should.
constexpr auto eventHeadroom = 256;

// How often the plugin's main-thread requests are looked at, absent a call to
// syncEditorWithProcessor().
constexpr auto serviceRateHz = 30;

// Each pass may raise another request (a callback that restarts, a restart that
// asks a callback); past this many the rest waits for the next service.
constexpr auto maxServicePasses = 8;

// The depth of calls into a hosted CLAP plugin made as its audio thread: inside
// process(), or inside a call the main thread makes while it holds process() off.
thread_local int audioThreadDepth = 0;

struct AudioThreadScope
{
    AudioThreadScope() noexcept { ++audioThreadDepth; }
    ~AudioThreadScope() { --audioThreadDepth; }

    AudioThreadScope(const AudioThreadScope&) = delete;
    AudioThreadScope& operator=(const AudioThreadScope&) = delete;
};

bool onAudioThread() noexcept
{
    return audioThreadDepth > 0;
}

bool onMainThread() noexcept
{
    return !onAudioThread() && eacp::Threads::isMainThread();
}

int blockLengthOf(const ProcessContext& context) noexcept
{
    if (!context.outputs.empty())
        return context.outputs[0].getNumSamples();

    if (!context.inputs.empty())
        return context.inputs[0].getNumSamples();

    return 0;
}

void clearOutputs(ProcessContext& context) noexcept
{
    for (auto& output: context.outputs)
        output.clear();
}

uint32_t timeIn(int sampleOffset, int numSamples) noexcept
{
    return static_cast<uint32_t>(
        std::clamp(sampleOffset, 0, std::max(0, numSamples - 1)));
}

int channelOf(int16_t channel) noexcept
{
    return channel >= 0 && channel < 16 ? channel : 0;
}

clap_event_header headerOf(uint32_t size, uint16_t type, uint32_t time) noexcept
{
    return {size, time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
}

// A name field the plugin may have filled without a terminator.
template <size_t Size>
std::string portName(const char (&field)[Size])
{
    return {field, strnlen(field, Size)};
}

const char* severityName(clap_log_severity severity) noexcept
{
    switch (severity)
    {
        case CLAP_LOG_WARNING:
            return "warning";
        case CLAP_LOG_ERROR:
            return "error";
        case CLAP_LOG_FATAL:
            return "fatal";
        case CLAP_LOG_HOST_MISBEHAVING:
            return "host misbehaving";
        case CLAP_LOG_PLUGIN_MISBEHAVING:
            return "plugin misbehaving";
        default:
            return "info";
    }
}
} // namespace

// The host's side of the C API: every function a clap_host and its extensions
// point at, each finding its instance through host_data.
struct CLAPHostedPlugin::Callbacks
{
    static CLAPHostedPlugin& of(const clap_host* host) noexcept
    {
        return *static_cast<CLAPHostedPlugin*>(host->host_data);
    }

    static const void* getExtension(const clap_host* host, const char* id) noexcept
    {
        if (id == nullptr)
            return nullptr;

        auto& self = of(host);
        auto is = [id](const char* wanted) { return std::strcmp(id, wanted) == 0; };

        if (is(CLAP_EXT_LOG))
            return &self.hostLog;

        if (is(CLAP_EXT_THREAD_CHECK))
            return &self.hostThreadCheck;

        if (is(CLAP_EXT_PARAMS))
            return &self.hostParams;

        if (is(CLAP_EXT_STATE))
            return &self.hostState;

        if (is(CLAP_EXT_LATENCY))
            return &self.hostLatency;

        if (is(CLAP_EXT_TAIL))
            return &self.hostTail;

        if (is(CLAP_EXT_AUDIO_PORTS))
            return &self.hostAudioPorts;

        if (is(CLAP_EXT_NOTE_PORTS))
            return &self.hostNotePorts;

        if (is(CLAP_EXT_TIMER_SUPPORT))
            return &self.hostTimers;

        if (is(CLAP_EXT_POSIX_FD_SUPPORT) && self.fdWatches != nullptr)
            return &self.hostFds;

        return nullptr;
    }

    // [thread-safe]: flagged here, run on the main thread.
    static void requestRestart(const clap_host* host) noexcept
    {
        auto& self = of(host);
        self.restartRequested.store(true);
        self.requestService();
    }

    // This host processes a prepared plugin whenever its own host runs a block,
    // so there is nothing to wake.
    static void requestProcess(const clap_host*) noexcept {}

    static void requestCallback(const clap_host* host) noexcept
    {
        auto& self = of(host);
        self.callbackRequested.store(true);
        self.requestService();
    }

    static void log(const clap_host*,
                    clap_log_severity severity,
                    const char* message) noexcept
    {
        if (message == nullptr || severity < CLAP_LOG_WARNING)
            return;

        std::fprintf(
            stderr, "CLAP plugin (%s): %s\n", severityName(severity), message);
    }

    static bool isMainThread(const clap_host*) noexcept { return onMainThread(); }
    static bool isAudioThread(const clap_host*) noexcept { return onAudioThread(); }

    static void rescanParams(const clap_host* host,
                             clap_param_rescan_flags flags) noexcept
    {
        try
        {
            of(host).rescanParameters(flags);
        }
        catch (...)
        {
        }
    }

    // Nothing of a parameter is kept here (no automation, no modulation).
    static void
        clearParam(const clap_host*, clap_id, clap_param_clear_flags) noexcept
    {
    }

    static void requestFlush(const clap_host* host) noexcept
    {
        auto& self = of(host);
        self.flushRequested.store(true);
        self.requestService();
    }

    static void markDirty(const clap_host* host) noexcept
    {
        auto& self = of(host);

        if (!self.loadingState)
            self.notifyHostStateChanged();
    }

    // Told while being activated, the latency is read once activate() returns;
    // told at any other time, which the spec does not allow, it takes a restart.
    static void latencyChanged(const clap_host* host) noexcept
    {
        auto& self = of(host);

        if (self.activating)
            return;

        self.restartRequested.store(true);
        self.requestService();
    }

    // The tail is asked whenever tailSamples() is.
    static void tailChanged(const clap_host*) noexcept {}

    static bool audioRescanSupported(const clap_host*, uint32_t) noexcept
    {
        return true;
    }

    static void rescanAudioPorts(const clap_host* host, uint32_t flags) noexcept
    {
        auto& self = of(host);

        try
        {
            self.updatePortNames();
        }
        catch (...)
        {
        }

        if ((flags & ~uint32_t {CLAP_AUDIO_PORTS_RESCAN_NAMES}) != 0)
            self.raiseIoChanged();
    }

    static uint32_t noteDialects(const clap_host*) noexcept
    {
        return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    }

    static void rescanNotePorts(const clap_host* host, uint32_t flags) noexcept
    {
        auto& self = of(host);

        try
        {
            self.updatePortNames();
        }
        catch (...)
        {
        }

        if ((flags & CLAP_NOTE_PORTS_RESCAN_ALL) != 0)
            self.raiseIoChanged();
    }

    static bool registerTimer(const clap_host* host,
                              uint32_t periodMs,
                              clap_id* timerId) noexcept
    {
        auto& self = of(host);

        if (timerId == nullptr || !onMainThread())
            return false;

        if (self.pluginTimers == nullptr)
            self.pluginTimers = self.pluginExtension<clap_plugin_timer_support>(
                CLAP_EXT_TIMER_SUPPORT);

        if (self.pluginTimers == nullptr || self.pluginTimers->on_timer == nullptr)
            return false;

        try
        {
            auto id = self.nextTimerId++;
            auto interval =
                static_cast<int64_t>(std::clamp<uint32_t>(periodMs, 1, 60000));
            auto* owner = &self;

            self.timers[id] = std::make_unique<eacp::Threads::Timer>(
                [owner, id] { owner->onTimer(id); }, eacp::Time::MS {interval});

            *timerId = id;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static bool unregisterTimer(const clap_host* host, clap_id timerId) noexcept
    {
        auto& self = of(host);

        if (!onMainThread())
            return false;

        auto found = self.timers.find(timerId);

        if (found == self.timers.end())
            return false;

        try
        {
            self.retiredTimers.push_back(std::move(found->second));
        }
        catch (...)
        {
        }

        self.timers.erase(found);
        return true;
    }

    static bool registerFd(const clap_host* host,
                           int fd,
                           clap_posix_fd_flags_t flags) noexcept
    {
        auto& self = of(host);

        if (self.fdWatches == nullptr || !onMainThread())
            return false;

        if (self.pluginFds == nullptr)
            self.pluginFds = self.pluginExtension<clap_plugin_posix_fd_support>(
                CLAP_EXT_POSIX_FD_SUPPORT);

        if (self.pluginFds == nullptr || self.pluginFds->on_fd == nullptr)
            return false;

        try
        {
            return self.fdWatches->add(fd, flags);
        }
        catch (...)
        {
            return false;
        }
    }

    static bool
        modifyFd(const clap_host* host, int fd, clap_posix_fd_flags_t flags) noexcept
    {
        auto& self = of(host);

        if (self.fdWatches == nullptr || !onMainThread())
            return false;

        try
        {
            return self.fdWatches->modify(fd, flags);
        }
        catch (...)
        {
            return false;
        }
    }

    static bool unregisterFd(const clap_host* host, int fd) noexcept
    {
        auto& self = of(host);

        if (self.fdWatches == nullptr || !onMainThread())
            return false;

        try
        {
            return self.fdWatches->remove(fd);
        }
        catch (...)
        {
            return false;
        }
    }
};

LoadResult CLAPHostedPlugin::create(PluginDescription description,
                                    std::shared_ptr<CLAPModule> module,
                                    const CreateFunction& create,
                                    const HostInfo& host)
{
    description.format = PluginFormat::CLAP;

    auto hosted = EA::makeOwned<CLAPHostedPlugin>(
        std::move(description), std::move(module), host.name);
    auto result = LoadResult();

    if (!hosted->initialise(create, result.error))
        return result;

    hosted->setEditorFactory(host.editors);
    result.plugin = std::move(hosted);
    return result;
}

CLAPHostedPlugin::CLAPHostedPlugin(PluginDescription description,
                                   std::shared_ptr<CLAPModule> moduleToUse,
                                   std::string hostNameToUse)
    : HostedPlugin(std::move(description))
    , module(std::move(moduleToUse))
    , hostName(std::move(hostNameToUse))
    , self(std::make_shared<CLAPHostedPlugin*>(this))
{
    setUpHost();

    fdWatches = CLAPFdWatches::create(
        [this](int fd, clap_posix_fd_flags_t flags)
        {
            if (plugin != nullptr && pluginFds != nullptr)
                pluginFds->on_fd(plugin, fd, flags);
        });
}

CLAPHostedPlugin::~CLAPHostedPlugin()
{
    // Nothing posted or ticking reaches this object from here on.
    serviceTimer.reset();
    self.reset();

    if (plugin != nullptr)
    {
        holdOffAudio();

        if (initialised)
            deactivate();

        // Valid after a failed init() too, which the spec allows.
        plugin->destroy(plugin);
        plugin = nullptr;
    }

    // Whatever the plugin left registered calls into a destroyed plugin no more.
    timers.clear();
    retiredTimers.clear();

    if (fdWatches != nullptr)
        fdWatches->clear();
}

void CLAPHostedPlugin::setUpHost()
{
    host.clap_version = CLAP_VERSION;
    host.host_data = this;
    host.name = hostName.c_str();
    host.vendor = "MakeASound";
    host.url = "";
    host.version = "1.0";
    host.get_extension = Callbacks::getExtension;
    host.request_restart = Callbacks::requestRestart;
    host.request_process = Callbacks::requestProcess;
    host.request_callback = Callbacks::requestCallback;

    hostLog.log = Callbacks::log;
    hostThreadCheck.is_main_thread = Callbacks::isMainThread;
    hostThreadCheck.is_audio_thread = Callbacks::isAudioThread;
    hostParams.rescan = Callbacks::rescanParams;
    hostParams.clear = Callbacks::clearParam;
    hostParams.request_flush = Callbacks::requestFlush;
    hostState.mark_dirty = Callbacks::markDirty;
    hostLatency.changed = Callbacks::latencyChanged;
    hostTail.changed = Callbacks::tailChanged;
    hostAudioPorts.is_rescan_flag_supported = Callbacks::audioRescanSupported;
    hostAudioPorts.rescan = Callbacks::rescanAudioPorts;
    hostNotePorts.supported_dialects = Callbacks::noteDialects;
    hostNotePorts.rescan = Callbacks::rescanNotePorts;
    hostTimers.register_timer = Callbacks::registerTimer;
    hostTimers.unregister_timer = Callbacks::unregisterTimer;
    hostFds.register_fd = Callbacks::registerFd;
    hostFds.modify_fd = Callbacks::modifyFd;
    hostFds.unregister_fd = Callbacks::unregisterFd;
}

bool CLAPHostedPlugin::initialise(const CreateFunction& create, std::string& error)
{
    plugin = create ? create(&host) : nullptr;

    if (plugin == nullptr)
    {
        error = "the factory could not create the plugin";
        return false;
    }

    if (plugin->init == nullptr || plugin->destroy == nullptr
        || plugin->activate == nullptr || plugin->deactivate == nullptr
        || plugin->start_processing == nullptr || plugin->stop_processing == nullptr
        || plugin->process == nullptr || plugin->get_extension == nullptr
        || plugin->on_main_thread == nullptr)
    {
        error = "the plugin leaves part of clap_plugin unset";

        // Not init()ed, but destroy() is the one way to let it go.
        if (plugin->destroy == nullptr)
            plugin = nullptr;

        return false;
    }

    if (!plugin->init(plugin))
    {
        error = "the plugin did not initialise";
        return false;
    }

    initialised = true;

    queryExtensions();

    // Destroyed with the instance; never activated.
    if (!readBusLayout())
    {
        error = "the plugin could not describe one of its audio ports";
        return false;
    }

    readNotePorts();
    buildParameters();
    ready = true;

    serviceTimer = std::make_unique<eacp::Threads::Timer>(
        [this] { serviceMainThread(); }, serviceRateHz);

    // Whatever the plugin asked for while it initialised.
    serviceMainThread();
    return true;
}

template <typename Extension>
const Extension* CLAPHostedPlugin::pluginExtension(const char* id) const noexcept
{
    if (plugin == nullptr)
        return nullptr;

    return static_cast<const Extension*>(plugin->get_extension(plugin, id));
}

void CLAPHostedPlugin::queryExtensions()
{
    params = pluginExtension<clap_plugin_params>(CLAP_EXT_PARAMS);
    state = pluginExtension<clap_plugin_state>(CLAP_EXT_STATE);
    stateContext =
        pluginExtension<clap_plugin_state_context>(CLAP_EXT_STATE_CONTEXT);
    audioPorts = pluginExtension<clap_plugin_audio_ports>(CLAP_EXT_AUDIO_PORTS);
    notePorts = pluginExtension<clap_plugin_note_ports>(CLAP_EXT_NOTE_PORTS);
    latency = pluginExtension<clap_plugin_latency>(CLAP_EXT_LATENCY);
    tail = pluginExtension<clap_plugin_tail>(CLAP_EXT_TAIL);
    gui = pluginExtension<clap_plugin_gui>(CLAP_EXT_GUI);

    // Incomplete extensions are as good as none.
    if (params != nullptr
        && (params->count == nullptr || params->get_info == nullptr
            || params->get_value == nullptr || params->flush == nullptr))
        params = nullptr;

    if (state != nullptr && (state->save == nullptr || state->load == nullptr))
        state = nullptr;

    if (stateContext != nullptr
        && (stateContext->save == nullptr || stateContext->load == nullptr))
        stateContext = nullptr;

    if (audioPorts != nullptr
        && (audioPorts->count == nullptr || audioPorts->get == nullptr))
        audioPorts = nullptr;

    if (notePorts != nullptr
        && (notePorts->count == nullptr || notePorts->get == nullptr))
        notePorts = nullptr;

    if (latency != nullptr && latency->get == nullptr)
        latency = nullptr;

    if (tail != nullptr && tail->get == nullptr)
        tail = nullptr;

    if (pluginTimers == nullptr)
        pluginTimers =
            pluginExtension<clap_plugin_timer_support>(CLAP_EXT_TIMER_SUPPORT);

    if (pluginFds == nullptr)
        pluginFds =
            pluginExtension<clap_plugin_posix_fd_support>(CLAP_EXT_POSIX_FD_SUPPORT);
}

bool CLAPHostedPlugin::readBusLayout()
{
    layout = BusLayout();
    auto portsDescribed = true;

    // CLAP has no port the host may switch off: every one is processed.
    if (audioPorts != nullptr)
    {
        for (auto isInput: {true, false})
        {
            auto& target = isInput ? layout.inputs : layout.outputs;
            const auto count = audioPorts->count(plugin, isInput);

            for (auto i = uint32_t {0}; i < count; ++i)
            {
                auto info = clap_audio_port_info();
                auto bus = Bus();
                bus.activeByDefault = true;

                if (audioPorts->get(plugin, i, isInput, &info))
                {
                    bus.name = portName(info.name);
                    bus.numChannels = static_cast<int>(
                        std::min(info.channel_count, uint32_t {64}));
                    bus.isMain = (info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0;
                }
                else
                {
                    portsDescribed = false;
                }

                target.add(bus);
            }
        }
    }

    if (notePorts != nullptr)
    {
        for (auto isInput: {true, false})
        {
            auto& target = isInput ? layout.midiInputs : layout.midiOutputs;
            const auto count = notePorts->count(plugin, isInput);

            for (auto i = uint32_t {0}; i < count; ++i)
            {
                auto info = clap_note_port_info();
                auto bus = MidiBus();

                if (notePorts->get(plugin, i, isInput, &info))
                    bus.name = portName(info.name);

                target.add(bus);
            }
        }
    }

    return portsDescribed;
}

void CLAPHostedPlugin::readNotePorts()
{
    notePortsIn.clear();

    if (notePorts == nullptr)
        return;

    const auto count = notePorts->count(plugin, true);

    for (auto i = uint32_t {0}; i < count; ++i)
    {
        auto info = clap_note_port_info();
        auto port = NotePort();

        // A port it would not describe keeps its slot and takes nothing.
        if (!notePorts->get(plugin, i, true, &info))
        {
            notePortsIn.push_back(port);
            continue;
        }

        port.takesMidi = (info.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0;

        auto takesClap = (info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0;
        port.prefersClap =
            takesClap
            && (info.preferred_dialect == CLAP_NOTE_DIALECT_CLAP || !port.takesMidi);

        notePortsIn.push_back(port);
    }
}

// Names only: the counts and channels are read again at the next prepare(), and
// the audio thread reads neither.
void CLAPHostedPlugin::updatePortNames()
{
    if (audioPorts != nullptr)
    {
        for (auto isInput: {true, false})
        {
            auto& buses = isInput ? layout.inputs : layout.outputs;

            for (auto i = 0; i < buses.size(); ++i)
            {
                auto info = clap_audio_port_info();

                if (audioPorts->get(
                        plugin, static_cast<uint32_t>(i), isInput, &info))
                    buses[i].name = portName(info.name);
            }
        }
    }

    if (notePorts != nullptr)
    {
        for (auto isInput: {true, false})
        {
            auto& buses = isInput ? layout.midiInputs : layout.midiOutputs;

            for (auto i = 0; i < buses.size(); ++i)
            {
                auto info = clap_note_port_info();

                if (notePorts->get(plugin, static_cast<uint32_t>(i), isInput, &info))
                    buses[i].name = portName(info.name);
            }
        }
    }
}

void CLAPHostedPlugin::buildParameters()
{
    if (params != nullptr)
    {
        auto seen = std::set<clap_id>();
        const auto count = params->count(plugin);

        for (auto i = uint32_t {0}; i < count; ++i)
        {
            auto info = clap_param_info();

            if (!params->get_info(plugin, i, &info) || !seen.insert(info.id).second)
                continue;

            // A hidden parameter is the plugin's own plumbing: never shown, and
            // reached through its own means rather than by index, as VST3's.
            if ((info.flags & CLAP_PARAM_IS_HIDDEN) != 0)
                continue;

            auto value = info.default_value;
            params->get_value(plugin, info.id, &value);

            storage.add(EA::makeOwned<CLAPHostedParameter>(
                *plugin,
                *params,
                info,
                value,
                static_cast<CLAPParameterSink&>(*this)));
        }
    }

    group.addAll(storage);

    for (auto& param: storage)
        byId.add(param.get());

    std::sort(byId.begin(),
              byId.end(),
              [](const CLAPHostedParameter* a, const CLAPHostedParameter* b)
              { return a->paramId() < b->paramId(); });

    setParameters(ParameterList {group, hostedPluginIdentity()});

    const auto numParams = static_cast<size_t>(storage.size());
    gestureOpen.assign(numParams, gestureClosed);
    audioWriteStamp.assign(numParams, 0);
    audioWriteSlot.assign(numParams, -1);
    editOverflow.assign(numParams, 0);

    // The queue, then every parameter's overflowed edit, then a little room.
    flushInput.reserve(storage.size() * 2 + 1024 + eventHeadroom);
    flushOutput.reserve(storage.size() * 3 + eventHeadroom);
}

CLAPHostedParameter* CLAPHostedPlugin::findParameter(clap_id id) const noexcept
{
    auto before = [](const CLAPHostedParameter* param, clap_id target)
    { return param->paramId() < target; };
    auto found = std::lower_bound(byId.begin(), byId.end(), id, before);

    if (found == byId.end() || (*found)->paramId() != id)
        return nullptr;

    return *found;
}

int CLAPHostedPlugin::indexOfParamId(clap_id id) const noexcept
{
    return parameters().indexOf(findParameter(id));
}

bool CLAPHostedPlugin::acceptsLayout(const BusLayout& proposed) const
{
    auto same = [](const Vector<Bus>& a, const Vector<Bus>& b)
    {
        if (a.size() != b.size())
            return false;

        for (auto i = 0; i < a.size(); ++i)
            if (a[i].numChannels != b[i].numChannels)
                return false;

        return true;
    };

    return same(proposed.inputs, layout.inputs)
           && same(proposed.outputs, layout.outputs);
}

void CLAPHostedPlugin::holdOffAudio() noexcept
{
    // With the audio side's store-then-load, one of the two sees the other:
    // either the block sees prepared off and leaves, or this sees it inside and
    // waits out the one block.
    prepared.store(false, std::memory_order_seq_cst);

    while (inAudioCall.load(std::memory_order_seq_cst))
        std::this_thread::yield();
}

bool CLAPHostedPlugin::activate()
{
    activating = true;
    auto ok = plugin->activate(
        plugin, sampleRate, 1, static_cast<uint32_t>(std::max(1, maxBlockSize)));
    activating = false;

    active = ok;
    processing.store(false);

    if (ok)
    {
        readLatency();

        // Idle once no block came for a few of the longest, and never sooner
        // than a host's buffer of a tenth of a second would allow.
        using namespace std::chrono;
        auto block = duration<double>(maxBlockSize / std::max(sampleRate, 1.0));
        idleAfter =
            std::max(duration_cast<steady_clock::duration>(block * 4),
                     duration_cast<steady_clock::duration>(milliseconds(100)));
        blocksSeen = blocksRun.load(std::memory_order_relaxed);
        blocksSeenAt = steady_clock::now();
    }

    return ok;
}

// With process() held off: stop_processing() as the audio thread would, since
// no block can run meanwhile, then deactivate().
void CLAPHostedPlugin::deactivate()
{
    if (processing.load())
    {
        auto scope = AudioThreadScope();
        plugin->stop_processing(plugin);
        processing.store(false);
    }

    if (active)
    {
        plugin->deactivate(plugin);
        active = false;
    }
}

void CLAPHostedPlugin::readLatency()
{
    auto now =
        latency != nullptr
            ? static_cast<int>(std::min(latency->get(plugin),
                                        uint32_t {std::numeric_limits<int>::max()}))
            : 0;
    auto previous = latencyNow.exchange(now);

    if (latencyKnown && previous != now)
    {
        raiseLatencyChanged();
        notifyHostLatencyChanged();
    }

    latencyKnown = true;
}

void CLAPHostedPlugin::restart()
{
    if (!active)
        return;

    // Edits queued for a block that never came go out through a flush, now the
    // plugin is inactive.
    holdOffAudio();
    deactivate();
    flushPendingEdits(false);

    // A port the plugin changed meanwhile takes the host's prepare(), which
    // sizes everything for it; until then it stays inactive.
    auto before = layout;
    auto portsDescribed = readBusLayout();
    readNotePorts();

    if (!(layout == before))
    {
        raiseIoChanged();
        return;
    }

    if (portsDescribed && activate())
        prepared.store(true, std::memory_order_seq_cst);
}

void CLAPHostedPlugin::prepare(const ProcessSpec& spec)
{
    if (!ready)
        return;

    holdOffAudio();
    deactivate();
    flushPendingEdits(false);

    // Inactive, the one time ports may be read for good. A port the plugin will
    // not describe leaves it inactive: its channels are unknown.
    auto portsDescribed = readBusLayout();
    readNotePorts();

    sampleRate = spec.sampleRate;
    maxBlockSize = std::max(spec.maxBlockSize, 0);
    steadyTime = 0;
    rewindFallbackPlayhead();
    sizeBuffers();

    if (!portsDescribed || sampleRate <= 0.0 || maxBlockSize <= 0)
        return;

    if (activate())
        prepared.store(true, std::memory_order_seq_cst);
}

void CLAPHostedPlugin::sizeBuffers()
{
    // Everything process() touches, sized here: a block must not allocate.
    inputBuffers.assign(static_cast<size_t>(layout.inputs.size()),
                        clap_audio_buffer {});
    outputBuffers.assign(static_cast<size_t>(layout.outputs.size()),
                         clap_audio_buffer {});
    inputTables.resize(static_cast<size_t>(layout.inputs.size()));
    outputTables.resize(static_cast<size_t>(layout.outputs.size()));

    auto channels = [](const Bus& bus)
    { return static_cast<size_t>(std::max(0, bus.numChannels)); };

    for (auto i = 0; i < layout.inputs.size(); ++i)
        inputTables[static_cast<size_t>(i)].assign(channels(layout.inputs[i]),
                                                   nullptr);

    for (auto i = 0; i < layout.outputs.size(); ++i)
        outputTables[static_cast<size_t>(i)].assign(channels(layout.outputs[i]),
                                                    nullptr);

    silence.assign(static_cast<size_t>(maxBlockSize), 0.f);
    scratch.assign(static_cast<size_t>(maxBlockSize), 0.f);

    constexpr auto midiCapacity = ProcessContext::defaultMidiCapacity;
    auto midiIn = std::max(1, layout.midiInputs.size()) * midiCapacity;
    auto midiOut = std::max(1, layout.midiOutputs.size()) * midiCapacity;

    inputEvents.reserve(storage.size() + 1024 + midiIn + eventHeadroom);
    outputEvents.reserve(storage.size() * 3 + midiOut + eventHeadroom);

    // Emptied: no write of this block's is in it any more.
    ++blockSerial;
}

int CLAPHostedPlugin::latencySamples() const noexcept
{
    return latencyNow.load();
}

int CLAPHostedPlugin::tailSamples() const noexcept
{
    if (tail == nullptr || plugin == nullptr)
        return 0;

    // [main-thread, audio-thread]: anything not the main thread is taken to be
    // the audio side, in a block or between blocks.
    auto ask = [this] { return tail->get(plugin); };
    auto samples = onMainThread() ? ask() : [&ask]
    {
        auto scope = AudioThreadScope();
        return ask();
    }();

    if (samples >= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
        return infiniteTail;

    return static_cast<int>(samples);
}

void CLAPHostedPlugin::bindAudio(ProcessContext& context, int numSamples) noexcept
{
    for (auto i = size_t {0}; i < inputBuffers.size(); ++i)
    {
        auto bus = static_cast<int>(i);
        auto* supplied =
            bus < context.inputs.size() && !context.inputs[bus].isEmpty()
                ? &context.inputs[bus]
                : nullptr;
        auto& table = inputTables[i];

        for (auto ch = size_t {0}; ch < table.size(); ++ch)
        {
            auto channel = static_cast<int>(ch);
            table[ch] = supplied != nullptr && channel < supplied->getNumChannels()
                            ? supplied->getChannelPointer(channel)
                            : silence.data();
        }

        auto& buffer = inputBuffers[i];
        buffer = clap_audio_buffer {};
        buffer.data32 = table.data();
        buffer.channel_count = static_cast<uint32_t>(table.size());

        // Every channel of an input not supplied is constant: silence.
        buffer.constant_mask = supplied != nullptr ? 0 : ~uint64_t {0};
    }

    for (auto i = size_t {0}; i < outputBuffers.size(); ++i)
    {
        auto bus = static_cast<int>(i);
        auto* supplied =
            bus < context.outputs.size() && !context.outputs[bus].isEmpty()
                ? &context.outputs[bus]
                : nullptr;
        auto& table = outputTables[i];

        for (auto ch = size_t {0}; ch < table.size(); ++ch)
        {
            auto channel = static_cast<int>(ch);
            table[ch] = supplied != nullptr && channel < supplied->getNumChannels()
                            ? supplied->getChannelPointer(channel)
                            : scratch.data();
        }

        auto& buffer = outputBuffers[i];
        buffer = clap_audio_buffer {};
        buffer.data32 = table.data();
        buffer.channel_count = static_cast<uint32_t>(table.size());
    }

    // A silent input is still read by the plugin: it must hold zeros.
    std::fill(silence.begin(), silence.begin() + numSamples, 0.f);
}

clap_event_param_value CLAPHostedPlugin::paramEventOf(clap_id id,
                                                      void* cookie,
                                                      double value,
                                                      uint32_t time) noexcept
{
    auto event = clap_event_param_value();
    event.header =
        headerOf(sizeof(clap_event_param_value), CLAP_EVENT_PARAM_VALUE, time);
    event.param_id = id;
    event.cookie = cookie;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    return event;
}

void CLAPHostedPlugin::process(ProcessContext& context) noexcept
{
    // Entered before prepared is read, so holdOffAudio() waits for this block.
    inAudioCall.store(true, std::memory_order_seq_cst);

    struct Leave
    {
        std::atomic<bool>& flag;
        ~Leave() { flag.store(false, std::memory_order_release); }
    } leave {inAudioCall};

    if (!prepared.load(std::memory_order_seq_cst))
    {
        clearOutputs(context);
        return;
    }

    auto numSamples = blockLengthOf(context);

    // activate() promised no longer block, and the stand-ins are sized to it.
    if (numSamples <= 0 || numSamples > maxBlockSize)
    {
        clearOutputs(context);
        return;
    }

    auto scope = AudioThreadScope();
    blocksRun.fetch_add(1, std::memory_order_relaxed);

    if (!processing.load(std::memory_order_relaxed))
    {
        if (!plugin->start_processing(plugin))
        {
            clearOutputs(context);
            return;
        }

        processing.store(true, std::memory_order_relaxed);
    }

    // This block carries whatever a flush would have.
    flushRequested.store(false);

    bindAudio(context, numSamples);
    fillTransport(playheadFor(context.playhead, numSamples, sampleRate));

    // The audio thread's own writes are in already; the message thread's edits
    // follow them and the MIDI follows both, so at one time a controller lands
    // after an edit of the same parameter. Sorted stably, that order holds.
    drainEdits();
    pushMidi(context, numSamples);
    inputEvents.sortByTime();

    auto data = clap_process();
    data.steady_time = steadyTime;
    data.frames_count = static_cast<uint32_t>(numSamples);
    data.transport = &transport;
    data.audio_inputs = inputBuffers.empty() ? nullptr : inputBuffers.data();
    data.audio_outputs = outputBuffers.empty() ? nullptr : outputBuffers.data();
    data.audio_inputs_count = static_cast<uint32_t>(inputBuffers.size());
    data.audio_outputs_count = static_cast<uint32_t>(outputBuffers.size());
    data.in_events = inputEvents.get();
    data.out_events = outputEvents.get();

    auto status = plugin->process(plugin, &data);

    readOutputEvents(context, numSamples);

    inputEvents.clear();
    outputEvents.clear();
    ++blockSerial;
    steadyTime += numSamples;

    if (status == CLAP_PROCESS_ERROR)
        clearOutputs(context);
}

bool CLAPHostedPlugin::setParameterFromAudioThread(int index,
                                                   float normalized) noexcept
{
    if (index < 0 || index >= storage.size() || !std::isfinite(normalized))
        return false;

    inAudioCall.store(true, std::memory_order_seq_cst);

    struct Leave
    {
        std::atomic<bool>& flag;
        ~Leave() { flag.store(false, std::memory_order_release); }
    } leave {inAudioCall};

    if (!prepared.load(std::memory_order_seq_cst))
        return false;

    auto& param = *storage[index];

    if (param.isReadOnly())
        return false;

    auto plain = param.plainFromNormalized(static_cast<double>(normalized));
    auto slot = static_cast<size_t>(index);

    // A second write this block replaces the first, so the list's room for one
    // per parameter is enough. The list is unsorted until the block runs, so
    // the slot is where it was added.
    if (audioWriteStamp[slot] == blockSerial)
    {
        auto* event = inputEvents.mutableAt(audioWriteSlot[slot]);

        if (event != nullptr && event->header.type == CLAP_EVENT_PARAM_VALUE
            && event->param.param_id == param.paramId())
        {
            event->param.value = plain;
            param.store(plain);
            return true;
        }
    }

    auto position = inputEvents.size();

    if (!inputEvents.add(
            paramEventOf(param.paramId(), param.cookie(), plain, 0).header))
        return false;

    audioWriteStamp[slot] = blockSerial;
    audioWriteSlot[slot] = position;
    param.store(plain);
    return true;
}

void CLAPHostedPlugin::drainEdits() noexcept
{
    auto generation = editGeneration.load(std::memory_order_acquire);
    auto edit = ParamEdit();

    // The list has room for a whole queue past the audio thread's own writes.
    while (pendingEdits.pop(edit))
        if (edit.generation == generation)
            inputEvents.add(
                paramEventOf(edit.id, edit.cookie, edit.value, 0).header);
}

void CLAPHostedPlugin::pushMidi(ProcessContext& context, int numSamples) noexcept
{
    auto buses = std::min(context.midiIn.size(), layout.midiInputs.size());
    buses = std::min(buses, static_cast<int>(notePortsIn.size()));

    // The list holds ProcessContext::defaultMidiCapacity events per note port
    // past the parameters'; every event takes one slot, so once one is refused
    // the rest of the block's MIDI would be too, and is dropped here.
    for (auto bus = 0; bus < buses; ++bus)
    {
        const auto& port = notePortsIn[static_cast<size_t>(bus)];

        for (const auto& event: context.midiIn[bus])
        {
            auto time = timeIn(event.sampleOffset, numSamples);
            auto channel = static_cast<int16_t>(event.channel & 0x0f);
            auto portIndex = static_cast<int16_t>(bus);

            const auto* on = event.asNoteOn();
            const auto* off = event.asNoteOff();

            if (port.prefersClap && (on != nullptr || off != nullptr))
            {
                auto note = clap_event_note();
                auto type = static_cast<uint16_t>(
                    on != nullptr ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF);
                note.header = headerOf(sizeof(clap_event_note), type, time);
                note.note_id = -1;
                note.port_index = portIndex;
                note.channel = channel;
                note.key =
                    static_cast<int16_t>(on != nullptr ? on->pitch : off->pitch);
                note.velocity = on != nullptr ? on->velocity : off->velocity;
                if (!inputEvents.add(note.header))
                    return;

                continue;
            }

            if (const auto* poly = event.asPolyAftertouch();
                poly != nullptr && port.prefersClap)
            {
                auto expression = clap_event_note_expression();
                expression.header = headerOf(sizeof(clap_event_note_expression),
                                             CLAP_EVENT_NOTE_EXPRESSION,
                                             time);
                expression.expression_id = CLAP_NOTE_EXPRESSION_PRESSURE;
                expression.note_id = -1;
                expression.port_index = portIndex;
                expression.channel = channel;
                expression.key = static_cast<int16_t>(poly->pitch);
                expression.value = poly->pressure;
                if (!inputEvents.add(expression.header))
                    return;

                continue;
            }

            // The rest has no CLAP note event: raw MIDI, where the port takes it.
            if (!port.takesMidi)
                continue;

            if (const auto* sysEx = event.asSysEx())
            {
                // F0 to F7. The bytes are the input event's own, which outlives
                // the block, so nothing is copied.
                if (sysEx->size <= 0)
                    continue;

                auto message = clap_event_midi_sysex();
                message.header = headerOf(
                    sizeof(clap_event_midi_sysex), CLAP_EVENT_MIDI_SYSEX, time);
                message.port_index = static_cast<uint16_t>(bus);
                message.buffer = sysEx->data.data();
                message.size = static_cast<uint32_t>(sysEx->size);
                if (!inputEvents.add(message.header))
                    return;

                continue;
            }

            auto bytes = MIDI::toBytes(event);

            if (bytes.size <= 0 || bytes.size > 3)
                continue;

            auto message = clap_event_midi();
            message.header =
                headerOf(sizeof(clap_event_midi), CLAP_EVENT_MIDI, time);
            message.port_index = static_cast<uint16_t>(bus);

            for (auto i = 0; i < bytes.size; ++i)
                message.data[i] = bytes.data[static_cast<size_t>(i)];

            if (!inputEvents.add(message.header))
                return;
        }
    }
}

void CLAPHostedPlugin::fillTransport(const Playhead& playhead) noexcept
{
    constexpr auto beatFactor = static_cast<double>(CLAP_BEATTIME_FACTOR);
    constexpr auto secondFactor = static_cast<double>(CLAP_SECTIME_FACTOR);

    auto beats = [](double quarters) noexcept
    { return static_cast<clap_beattime>(std::llround(quarters * beatFactor)); };

    transport = clap_event_transport();
    transport.header =
        headerOf(sizeof(clap_event_transport), CLAP_EVENT_TRANSPORT, 0);

    transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
                      | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE
                      | CLAP_TRANSPORT_HAS_TIME_SIGNATURE;

    if (playhead.isPlaying)
        transport.flags |= CLAP_TRANSPORT_IS_PLAYING;

    if (playhead.isRecording)
        transport.flags |= CLAP_TRANSPORT_IS_RECORDING;

    if (playhead.isLooping)
        transport.flags |= CLAP_TRANSPORT_IS_LOOP_ACTIVE;

    transport.tempo = playhead.bpm;
    transport.song_pos_beats = beats(playhead.ppqPosition);
    transport.bar_start = beats(playhead.barStartPpq);

    auto seconds = sampleRate > 0.0
                       ? static_cast<double>(playhead.sampleTime) / sampleRate
                       : 0.0;
    transport.song_pos_seconds =
        static_cast<clap_sectime>(std::llround(seconds * secondFactor));

    transport.loop_start_beats = beats(playhead.loopStartPpq);
    transport.loop_end_beats = beats(playhead.loopEndPpq);

    if (playhead.bpm > 0.0)
    {
        auto toSeconds = [&](double quarters) noexcept
        {
            return static_cast<clap_sectime>(
                std::llround(quarters * 60.0 / playhead.bpm * secondFactor));
        };

        transport.loop_start_seconds = toSeconds(playhead.loopStartPpq);
        transport.loop_end_seconds = toSeconds(playhead.loopEndPpq);
    }

    auto numerator = std::clamp(playhead.timeSignature.numerator, 1, 0xffff);
    auto denominator = std::clamp(playhead.timeSignature.denominator, 1, 0xffff);
    transport.tsig_num = static_cast<uint16_t>(numerator);
    transport.tsig_denom = static_cast<uint16_t>(denominator);

    // Quarters per bar, so the bar the bar start falls in.
    auto barLength = 4.0 * numerator / denominator;
    transport.bar_number =
        static_cast<int32_t>(std::floor(playhead.barStartPpq / barLength + 1e-9));
}

bool CLAPHostedPlugin::gestureRoomFor(int events) const noexcept
{
    // Past what is asked, one slot stays free for each reported gesture's end.
    return static_cast<int>(gestures.freeSlots()) >= events + reportedOpen;
}

void CLAPHostedPlugin::pushGesture(GestureEvent::Kind kind,
                                   int index,
                                   float normalized) noexcept
{
    // Room was made sure of first: gestureRoomFor, or an end's reserved slot.
    if (gestures.push({kind, index, normalized}))
        gesturesPending.store(true, std::memory_order_release);
}

void CLAPHostedPlugin::readOutputParameter(const clap_event_header& header) noexcept
{
    auto isGesture = header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                     || header.type == CLAP_EVENT_PARAM_GESTURE_END;
    auto id =
        isGesture
            ? reinterpret_cast<const clap_event_param_gesture&>(header).param_id
            : reinterpret_cast<const clap_event_param_value&>(header).param_id;

    auto* param = findParameter(id);

    if (param == nullptr)
        return;

    auto index = param->index();

    if (index < 0 || index >= static_cast<int>(gestureOpen.size()))
        return;

    auto& open = gestureOpen[static_cast<size_t>(index)];

    // A full queue drops a gesture whole, at its begin: its values and end go
    // with it, and a reported one's end always has its slot.
    if (header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN)
    {
        if (open != gestureClosed)
            return;

        if (gestureRoomFor(2))
        {
            pushGesture(GestureEvent::begin, index, 0.f);
            open = gestureReported;
            ++reportedOpen;
        }
        else
        {
            open = gestureUnreported;
        }

        return;
    }

    if (header.type == CLAP_EVENT_PARAM_GESTURE_END)
    {
        if (open == gestureReported)
        {
            pushGesture(GestureEvent::end, index, 0.f);
            --reportedOpen;
        }

        open = gestureClosed;
        return;
    }

    const auto& value = reinterpret_cast<const clap_event_param_value&>(header);

    // A value for one note, key, channel or port is not the parameter's own.
    if (value.note_id != -1 || value.key != -1 || value.channel != -1
        || value.port_index != -1)
        return;

    param->store(value.value);

    // Inside a reported gesture it is the user's edit. Outside one it is the
    // plugin's own output, kept and nothing more, as VST3's processor output is.
    if (open == gestureReported && gestureRoomFor(1))
        pushGesture(GestureEvent::perform, index, param->getNormalized());
}

void CLAPHostedPlugin::readOutputEvents(ProcessContext& context,
                                        int numSamples) noexcept
{
    for (auto i = 0; i < outputEvents.size(); ++i)
    {
        const auto* header = outputEvents.at(i);

        if (header == nullptr || header->space_id != CLAP_CORE_EVENT_SPACE_ID)
            continue;

        auto last = static_cast<uint32_t>(std::max(0, numSamples - 1));
        auto time = static_cast<int>(std::min(header->time, last));

        auto busFor = [&](int port) noexcept -> MIDI::Buffer*
        {
            auto bus = port < 0 ? 0 : port;

            if (bus >= context.midiOut.size())
                return nullptr;

            auto& target = context.midiOut[bus];

            // Reserved in prepare(); a full buffer drops rather than grows.
            return target.size() < target.capacity() ? &target : nullptr;
        };

        switch (header->type)
        {
            case CLAP_EVENT_PARAM_VALUE:
            case CLAP_EVENT_PARAM_GESTURE_BEGIN:
            case CLAP_EVENT_PARAM_GESTURE_END:
                readOutputParameter(*header);
                break;

            case CLAP_EVENT_NOTE_ON:
            case CLAP_EVENT_NOTE_OFF:
            case CLAP_EVENT_NOTE_CHOKE:
            {
                const auto& note = reinterpret_cast<const clap_event_note&>(*header);
                auto* target = busFor(note.port_index);

                if (target == nullptr || note.key < 0 || note.key > 127)
                    break;

                auto channel = channelOf(note.channel);
                auto velocity =
                    static_cast<float>(std::clamp(note.velocity, 0.0, 1.0));

                if (header->type == CLAP_EVENT_NOTE_ON)
                    target->add(
                        MIDI::Event::noteOn(channel, note.key, velocity, time));
                else
                    target->add(MIDI::Event::noteOff(
                        channel,
                        note.key,
                        header->type == CLAP_EVENT_NOTE_OFF ? velocity : 0.f,
                        time));
                break;
            }

            case CLAP_EVENT_MIDI:
            {
                const auto& midi = reinterpret_cast<const clap_event_midi&>(*header);
                auto* target = busFor(midi.port_index);

                if (target == nullptr)
                    break;

                if (auto converted = MIDI::convertMidi(midi.data, 3, time))
                    target->add(*converted);
                break;
            }

            case CLAP_EVENT_MIDI_SYSEX:
            {
                // Copied into the event now, while the plugin's bytes are valid.
                // Past MIDI::SysEx::maxBytes it is dropped, as the other hosts
                // drop it.
                const auto& sysEx =
                    reinterpret_cast<const clap_event_midi_sysex&>(*header);
                auto* target = busFor(sysEx.port_index);
                auto size = sysEx.size;

                if (target != nullptr && sysEx.buffer != nullptr && size > 0
                    && size <= static_cast<uint32_t>(MIDI::SysEx::maxBytes))
                    target->add(MIDI::Event::sysEx(
                        sysEx.buffer, static_cast<int>(size), time));
                break;
            }

            default:
                break;
        }
    }
}

void CLAPHostedPlugin::dispatchGesture(const GestureEvent& event) noexcept
{
    auto* listener = hostEditListener();

    if (listener == nullptr)
        return;

    switch (event.kind)
    {
        case GestureEvent::begin:
            listener->beginParameterEdit(event.index);
            break;
        case GestureEvent::perform:
            listener->performParameterEdit(event.index, event.normalized);
            break;
        case GestureEvent::end:
            listener->endParameterEdit(event.index);
            break;
    }
}

// Not nested: a listener editing from inside its callback queues gestures this
// loop then takes, in order.
void CLAPHostedPlugin::drainGestures() noexcept
{
    if (draining)
        return;

    draining = true;

    while (gesturesPending.exchange(false, std::memory_order_acq_rel))
    {
        auto event = GestureEvent();

        while (gestures.pop(event))
            dispatchGesture(event);
    }

    draining = false;
}

void CLAPHostedPlugin::parameterEdited(const CLAPHostedParameter& param,
                                       double plain) noexcept
{
    if (!ready)
        return;

    queueEdit(param, plain);

    // Active, the plugin hears it in its next block, or in a flush once idle.
    if (active)
    {
        editsWaiting = true;
        return;
    }

    // Inactive, no block comes, and a flush on this thread is how it hears it.
    flushPendingEdits(false);
    drainGestures();
}

void CLAPHostedPlugin::queueEdit(const CLAPHostedParameter& param,
                                 double plain) noexcept
{
    // Older refusals first, so the queue keeps the edits' order.
    retryOverflowedEdits();

    auto generation = editGeneration.load(std::memory_order_relaxed);

    if (pendingEdits.push({param.paramId(), param.cookie(), plain, generation}))
        return;

    // Full: kept per parameter, and sent at its latest value when there is room.
    auto index = param.index();

    if (index >= 0 && index < static_cast<int>(editOverflow.size()))
    {
        editOverflow[static_cast<size_t>(index)] = 1;
        overflowPending = true;
    }
}

void CLAPHostedPlugin::retryOverflowedEdits() noexcept
{
    if (!overflowPending)
        return;

    auto generation = editGeneration.load(std::memory_order_relaxed);

    for (auto i = size_t {0}; i < editOverflow.size(); ++i)
    {
        if (editOverflow[i] == 0)
            continue;

        const auto& param = *storage[static_cast<int>(i)];

        if (!pendingEdits.push(
                {param.paramId(), param.cookie(), param.plainValue(), generation}))
            return;

        editOverflow[i] = 0;
    }

    overflowPending = false;
}

void CLAPHostedPlugin::flushPendingEdits(bool requested) noexcept
{
    if (flushing)
        return;

    flushing = true;

    // Each pass takes what the one before let in (an edit made from inside
    // the flush, by the plugin's own callbacks); a bounded number of them.
    for (auto pass = 0; pass < maxServicePasses; ++pass)
    {
        auto generation = editGeneration.load(std::memory_order_acquire);
        auto edit = ParamEdit();
        flushInput.clear();

        // Sized for the whole queue and every parameter's overflow, so no add
        // is refused.
        while (pendingEdits.pop(edit))
            if (edit.generation == generation)
                flushInput.add(
                    paramEventOf(edit.id, edit.cookie, edit.value, 0).header);

        for (auto i = size_t {0}; overflowPending && i < editOverflow.size(); ++i)
        {
            if (editOverflow[i] == 0)
                continue;

            const auto& param = *storage[static_cast<int>(i)];
            flushInput.add(
                paramEventOf(param.paramId(), param.cookie(), param.plainValue(), 0)
                    .header);
            editOverflow[i] = 0;
        }

        overflowPending = false;

        if (params == nullptr || (flushInput.empty() && !requested))
            break;

        requested = false;
        flushInput.sortByTime();

        // [active ? audio-thread : main-thread], never beside a block: the
        // caller has seen to that.
        if (active)
        {
            auto scope = AudioThreadScope();
            params->flush(plugin, flushInput.get(), flushOutput.get());
        }
        else
        {
            params->flush(plugin, flushInput.get(), flushOutput.get());
        }

        // Into the gesture queue, this thread its producer while the audio
        // thread is kept out; dispatched later, outside this flush.
        for (auto i = 0; i < flushOutput.size(); ++i)
        {
            const auto* header = flushOutput.at(i);

            if (header != nullptr && header->space_id == CLAP_CORE_EVENT_SPACE_ID
                && (header->type == CLAP_EVENT_PARAM_VALUE
                    || header->type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                    || header->type == CLAP_EVENT_PARAM_GESTURE_END))
                readOutputParameter(*header);
        }

        flushOutput.clear();
    }

    flushInput.clear();
    flushing = false;
}

bool CLAPHostedPlugin::audioIdle() noexcept
{
    auto now = std::chrono::steady_clock::now();
    auto seen = blocksRun.load(std::memory_order_relaxed);

    if (seen != blocksSeen)
    {
        blocksSeen = seen;
        blocksSeenAt = now;
        return false;
    }

    return now - blocksSeenAt >= idleAfter;
}

void CLAPHostedPlugin::flushWhileIdle() noexcept
{
    // A block arriving meanwhile outputs silence, as during a restart; idle,
    // none is expected.
    holdOffAudio();

    editsWaiting = false;
    flushPendingEdits(flushRequested.exchange(false));

    prepared.store(true, std::memory_order_seq_cst);
}

void CLAPHostedPlugin::requestService() noexcept
{
    // From another thread the service timer picks it up: posting allocates.
    if (!onMainThread() || servicePosted || self == nullptr)
        return;

    try
    {
        auto weak = std::weak_ptr<CLAPHostedPlugin*>(self);
        servicePosted = true;

        eacp::Threads::callAsync(
            [weak]
            {
                if (auto alive = weak.lock())
                {
                    (*alive)->servicePosted = false;
                    (*alive)->serviceMainThread();
                }
            });
    }
    catch (...)
    {
        servicePosted = false;
    }
}

void CLAPHostedPlugin::serviceMainThread()
{
    // Not nested: a plugin's callback may well pump the loop this runs on.
    if (!ready || servicing || plugin == nullptr)
        return;

    servicing = true;

    // A tick on the stack (one pumping a modal loop) may be a retired timer's.
    if (timerDepth == 0)
        retiredTimers.clear();

    retryOverflowedEdits();
    drainGestures();

    // Looked at every service, so the time since the last block is known.
    auto idle = active && audioIdle();

    for (auto pass = 0; pass < maxServicePasses; ++pass)
    {
        auto any = false;

        if (callbackRequested.exchange(false))
        {
            any = true;
            plugin->on_main_thread(plugin);
        }

        if (restartRequested.exchange(false))
        {
            any = true;
            idle = false;
            restart();
        }

        if (!active && flushRequested.exchange(false))
        {
            any = true;
            flushPendingEdits(true);
        }

        // Active, the next block carries a flush request and the queued edits;
        // with no block lately, a flush as the audio thread does.
        if (idle && active && prepared.load()
            && (flushRequested.load() || editsWaiting))
        {
            any = true;
            flushWhileIdle();
        }

        if (!any)
            break;
    }

    drainGestures();
    servicing = false;
}

void CLAPHostedPlugin::syncEditorWithProcessor()
{
    serviceMainThread();
}

void CLAPHostedPlugin::onTimer(clap_id id)
{
    if (plugin == nullptr || pluginTimers == nullptr || timers.count(id) == 0)
        return;

    ++timerDepth;
    pluginTimers->on_timer(plugin, id);
    --timerDepth;
}

void CLAPHostedPlugin::rescanParameters(uint32_t flags)
{
    if (!ready)
        return;

    auto all = (flags & CLAP_PARAM_RESCAN_ALL) != 0;

    if (all || (flags & CLAP_PARAM_RESCAN_VALUES) != 0)
    {
        refreshValues();

        // A load this host asked for re-reads them itself, unannounced.
        if (!loadingState)
            notifyHostParameterValuesChanged();
    }

    // Reported only when a name moved, so a plugin rescanning defensively costs
    // the host nothing.
    if ((all || (flags & CLAP_PARAM_RESCAN_INFO) != 0) && refreshInfo())
    {
        raiseParameterInfoChanged();
        refreshParameterNames();
    }

    // The list keeps the set it has (swapping parameters would dangle every
    // index a host holds); a new set takes a reload, which this asks for.
    if (all && parameterSetChanged())
        raiseIoChanged();
}

void CLAPHostedPlugin::refreshValues()
{
    if (params == nullptr)
        return;

    for (auto& param: storage)
    {
        auto value = 0.0;

        if (params->get_value(plugin, param->paramId(), &value))
            param->store(value);
    }
}

void CLAPHostedPlugin::refreshParameterValue(int index)
{
    if (params == nullptr || index < 0 || index >= storage.size())
        return;

    auto value = 0.0;

    if (params->get_value(plugin, storage[index]->paramId(), &value))
        storage[index]->store(value);
}

bool CLAPHostedPlugin::refreshInfo()
{
    if (params == nullptr)
        return false;

    // By id, not position: nothing promises get_info's order.
    auto changed = false;
    const auto count = params->count(plugin);

    for (auto i = uint32_t {0}; i < count; ++i)
    {
        auto info = clap_param_info();

        if (!params->get_info(plugin, i, &info))
            continue;

        if (auto* param = findParameter(info.id))
            changed = param->updateInfo(info) || changed;
    }

    return changed;
}

bool CLAPHostedPlugin::parameterSetChanged() const
{
    auto now = std::vector<clap_id>();

    if (params != nullptr)
    {
        const auto count = params->count(plugin);

        for (auto i = uint32_t {0}; i < count; ++i)
        {
            auto info = clap_param_info();

            if (params->get_info(plugin, i, &info)
                && (info.flags & CLAP_PARAM_IS_HIDDEN) == 0)
                now.push_back(info.id);
        }
    }

    std::sort(now.begin(), now.end());
    now.erase(std::unique(now.begin(), now.end()), now.end());

    if (static_cast<int>(now.size()) != byId.size())
        return true;

    for (auto i = 0; i < byId.size(); ++i)
        if (byId[i]->paramId() != now[static_cast<size_t>(i)])
            return true;

    return false;
}

std::string CLAPHostedPlugin::saveState(StateContext context)
{
    if (plugin == nullptr || !ready)
        return {};

    auto stream = CLAPOutStream();
    auto saved = false;

    if (stateContext != nullptr)
        saved = stateContext->save(plugin,
                                   stream.get(),
                                   context == StateContext::Preset
                                       ? CLAP_STATE_CONTEXT_FOR_PRESET
                                       : CLAP_STATE_CONTEXT_FOR_PROJECT);
    else if (state != nullptr)
        saved = state->save(plugin, stream.get());

    return saved ? std::move(stream.data) : std::string();
}

void CLAPHostedPlugin::loadState(std::string_view data, StateContext context)
{
    if (plugin == nullptr || !ready || data.empty()
        || (state == nullptr && stateContext == nullptr))
        return;

    // An edit still queued is older than the state: the next block or flush
    // drops it rather than apply it over what is loaded.
    editGeneration.fetch_add(1, std::memory_order_release);
    std::fill(editOverflow.begin(), editOverflow.end(), uint8_t {0});
    overflowPending = false;
    editsWaiting = false;

    auto stream = CLAPInStream(data);
    loadingState = true;

    if (stateContext != nullptr)
        stateContext->load(plugin,
                           stream.get(),
                           context == StateContext::Preset
                               ? CLAP_STATE_CONTEXT_FOR_PRESET
                               : CLAP_STATE_CONTEXT_FOR_PROJECT);
    else
        state->load(plugin, stream.get());

    loadingState = false;

    // The plugin now holds values the parameters have never seen.
    refreshValues();
}

} // namespace MakeASound::Hosting
