#include "Adapter.h"
#include "Events.h"
#include "Gui.h"
#include "Ports.h"
#include "State.h"
#include "../Realtime/MessageThread.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <utility>

namespace MakeASound::CLAP
{

namespace
{
bool init(const clap_plugin* plugin) noexcept
{
    return Adapter::from(plugin)->init();
}

void destroy(const clap_plugin* plugin) noexcept
{
    delete Adapter::from(plugin);
}

bool activate(const clap_plugin* plugin,
              double sampleRate,
              uint32_t minFrames,
              uint32_t maxFrames) noexcept
{
    return Adapter::from(plugin)->activate(sampleRate, minFrames, maxFrames);
}

void deactivate(const clap_plugin* plugin) noexcept
{
    Adapter::from(plugin)->deactivate();
}

bool startProcessing(const clap_plugin* plugin) noexcept
{
    return Adapter::from(plugin)->startProcessing();
}

void stopProcessing(const clap_plugin* plugin) noexcept
{
    Adapter::from(plugin)->stopProcessing();
}

void reset(const clap_plugin* plugin) noexcept
{
    Adapter::from(plugin)->reset();
}

clap_process_status process(const clap_plugin* plugin,
                            const clap_process* data) noexcept
{
    if (data == nullptr)
        return CLAP_PROCESS_ERROR;

    return Adapter::from(plugin)->process(*data);
}

const void* getExtension(const clap_plugin* plugin, const char* id) noexcept
{
    return id != nullptr ? Adapter::from(plugin)->extension(id) : nullptr;
}

void onMainThread(const clap_plugin* plugin) noexcept
{
    Adapter::from(plugin)->onMainThread();
}

uint32_t latencyGet(const clap_plugin* plugin) noexcept
{
    return Adapter::from(plugin)->latency();
}

uint32_t tailGet(const clap_plugin* plugin) noexcept
{
    return Adapter::from(plugin)->tail();
}

bool hardRealtime(const clap_plugin*) noexcept
{
    return false;
}

bool renderSet(const clap_plugin* plugin, clap_plugin_render_mode mode) noexcept
{
    return Adapter::from(plugin)->setRenderMode(mode);
}

void trackInfoChanged(const clap_plugin* plugin) noexcept
{
    Adapter::from(plugin)->trackInfoChanged();
}

constexpr auto latencyExtension = clap_plugin_latency {latencyGet};
constexpr auto tailExtension = clap_plugin_tail {tailGet};
constexpr auto renderExtension = clap_plugin_render {hardRealtime, renderSet};
constexpr auto trackInfoExtension = clap_plugin_track_info {trackInfoChanged};

bool is(const char* id, const char* wanted) noexcept
{
    return std::strcmp(id, wanted) == 0;
}
} // namespace

Adapter::Adapter(const clap_host* hostToUse,
                 const clap_plugin_descriptor* descriptor,
                 OwningPointer<Plugin> plugin,
                 ParameterIdentity identity)
    : host(hostToUse)
    , pluginWrapper(std::move(plugin), PluginFormat::CLAP, identity)
{
    clap.desc = descriptor;
    clap.plugin_data = this;
    clap.init = CLAP::init;
    clap.destroy = CLAP::destroy;
    clap.activate = CLAP::activate;
    clap.deactivate = CLAP::deactivate;
    clap.start_processing = CLAP::startProcessing;
    clap.stop_processing = CLAP::stopProcessing;
    clap.reset = CLAP::reset;
    clap.process = CLAP::process;
    clap.get_extension = getExtension;
    clap.on_main_thread = CLAP::onMainThread;

    this->plugin().setHostEditListener(this);

    const auto& list = this->plugin().parameters();
    const auto space = list.identity().clapValues;

    lastPluginValue = std::make_unique<std::atomic<float>[]>(
        static_cast<size_t>(std::max(0, list.size())));
    editSlots =
        std::make_unique<EditSlot[]>(static_cast<size_t>(std::max(0, list.size())));
    mappings.reserveAtLeast(list.size());

    for (auto i = 0; i < list.size(); ++i)
    {
        mappings.add(valueMappingFor(list[i], space));

        if (list.isListedInCLAP(i))
        {
            listed.add(i);
            byClapId.add({list.entry(i).clapId, i});
        }
    }

    std::sort(byClapId.begin(),
              byClapId.end(),
              [](const ClapSlot& a, const ClapSlot& b) { return a.id < b.id; });

    midiCursors.resize(pluginWrapper.busLayout().midiOutputs.size());
    takeAllAsBaseline();
}

Adapter::~Adapter()
{
    detachHostLoop(hostLoop);
    plugin().setHostEditListener(nullptr);
}

Adapter* Adapter::from(const clap_plugin* plugin) noexcept
{
    return static_cast<Adapter*>(plugin->plugin_data);
}

Plugin& Adapter::plugin() noexcept
{
    return pluginWrapper.plugin();
}

PluginWrapper& Adapter::wrapper() noexcept
{
    return pluginWrapper;
}

template <typename Extension>
const Extension* Adapter::hostExtension(const char* id) const noexcept
{
    if (host == nullptr || host->get_extension == nullptr)
        return nullptr;

    return static_cast<const Extension*>(host->get_extension(host, id));
}

bool Adapter::isMainThread() const noexcept
{
    return threadCheck == nullptr || threadCheck->is_main_thread(host);
}

bool Adapter::isAudioThread() const noexcept
{
    return threadCheck == nullptr || threadCheck->is_audio_thread(host);
}

bool Adapter::init() noexcept
{
    try
    {
        adoptHostMessageThread();
    }
    catch (...)
    {
        return false;
    }

    hostParams = hostExtension<clap_host_params>(CLAP_EXT_PARAMS);
    hostState = hostExtension<clap_host_state>(CLAP_EXT_STATE);
    hostLatency = hostExtension<clap_host_latency>(CLAP_EXT_LATENCY);
    threadCheck = hostExtension<clap_host_thread_check>(CLAP_EXT_THREAD_CHECK);
    hostTrackInfo = hostExtension<clap_host_track_info>(CLAP_EXT_TRACK_INFO);

    if (hostTrackInfo == nullptr)
        hostTrackInfo =
            hostExtension<clap_host_track_info>(CLAP_EXT_TRACK_INFO_COMPAT);

    assert(isMainThread());

    // What callOnMessageThread queues runs off the host's own loop (Linux).
    attachHostLoop(hostLoop, host);

    try
    {
        // Before the host activates, so the plugin can prepare by host.
        if (host != nullptr && host->name != nullptr)
            pluginWrapper.setHostName(host->name);

        readTrackInfo();
    }
    catch (...)
    {
        return false;
    }

    return true;
}

bool Adapter::activate(double rate, uint32_t, uint32_t maxFrames) noexcept
{
    assert(isMainThread());

    if (active || rate <= 0.0 || maxFrames == 0)
        return false;

    try
    {
        pluginWrapper.prepare(
            rate,
            static_cast<int>(std::min(
                maxFrames, static_cast<uint32_t>(std::numeric_limits<int>::max()))));
    }
    catch (...)
    {
        return false;
    }

    pluginWrapper.reset();
    sampleRate = rate;
    prepared = true;
    active = true;

    // Being activated is the one time a latency change may be told, rather than
    // restarted for.
    pluginWrapper.takeLatencyChanged();
    auto current = std::max(0, plugin().latencySamples());
    auto previous = knownLatency.exchange(current);
    restartRequested = false;

    if (previous >= 0 && previous != current && hostLatency != nullptr)
        hostLatency->changed(host);

    return true;
}

void Adapter::deactivate() noexcept
{
    assert(isMainThread());
    active = false;

    // Before the next activation: the host rescans everything it knows.
    if (std::exchange(mappingsStale, false))
    {
        rebuildMappings();

        if (hostParams != nullptr)
            hostParams->rescan(host, CLAP_PARAM_RESCAN_ALL);
    }
}

bool Adapter::startProcessing() noexcept
{
    pluginWrapper.reset();
    processing = true;
    return true;
}

// Audio thread, where request_flush may not be called: an edit queued while
// processing would otherwise wait for a block that never comes.
void Adapter::stopProcessing() noexcept
{
    processing = false;

    if (editsPending.load() && host != nullptr && !flushOnMainThread.exchange(true))
        host->request_callback(host);
}

void Adapter::reset() noexcept
{
    pluginWrapper.reset();
}

void Adapter::silenceOutputs(const clap_process& data) noexcept
{
    for (auto bus = uint32_t {0}; bus < data.audio_outputs_count; ++bus)
    {
        auto& output = data.audio_outputs[bus];

        if (output.data32 == nullptr)
            continue;

        for (auto ch = uint32_t {0}; ch < output.channel_count; ++ch)
            if (auto* channel = output.data32[ch])
                std::fill_n(channel, data.frames_count, 0.f);
    }
}

clap_process_status Adapter::process(const clap_process& data) noexcept
{
    assert(isAudioThread());

    if (!prepared)
    {
        silenceOutputs(data);
        return CLAP_PROCESS_CONTINUE;
    }

    auto numSamples = static_cast<int>(std::min(
        data.frames_count, static_cast<uint32_t>(std::numeric_limits<int>::max())));

    // The wrapper's documented order: parameters, playhead and offline, MIDI,
    // buses, process.
    if (data.in_events != nullptr)
        collectParameters(*data.in_events, numSamples);

    pluginWrapper.applyParameterChanges(numSamples);

    // Not echoed back: neither a value the host sent nor a ramp carried.
    for (auto index: pluginWrapper.appliedParameters())
        takeAsBaseline(index);

    pluginWrapper.setPlayhead(data.transport != nullptr
                                  ? toPlayhead(*data.transport, sampleRate)
                                  : Playhead {});
    pluginWrapper.setOffline(offline.load(std::memory_order_relaxed));

    pluginWrapper.clearMidi();

    if (data.in_events != nullptr)
        collectMidi(*data.in_events, numSamples);

    pluginWrapper.sortMidiInByOffset();

    if (numSamples > 0)
    {
        bindBuses(data);
        pluginWrapper.process();

        // A plugin may write each bus out of order; CLAP's list must not be.
        pluginWrapper.sortMidiOutByOffset();

        for (auto bus = uint32_t {0}; bus < data.audio_outputs_count; ++bus)
            data.audio_outputs[bus].constant_mask = 0;
    }

    if (data.out_events != nullptr)
    {
        emitEdits(*data.out_events);
        emitPluginMoves(*data.out_events);

        if (numSamples > 0)
            emitMidi(*data.out_events, numSamples);
    }

    if (!restartRequested.load()
        && std::max(0, plugin().latencySamples()) != knownLatency.load())
        requestLatencyCheck();

    return CLAP_PROCESS_CONTINUE;
}

void Adapter::bindBuses(const clap_process& data) noexcept
{
    const auto& layout = pluginWrapper.busLayout();
    auto numSamples = static_cast<int>(data.frames_count);

    // A null table, as null channel pointers, is the wrapper's to fill with
    // silent scratch of the bus's width.
    auto channelsOf = [](const clap_audio_buffer& bus)
    { return static_cast<int>(std::min(bus.channel_count, uint32_t {1024})); };

    auto numInputs =
        std::min(layout.inputs.size(), static_cast<int>(data.audio_inputs_count));

    for (auto bus = 0; bus < numInputs; ++bus)
    {
        const auto& input = data.audio_inputs[bus];
        pluginWrapper.bindInput(bus, input.data32, channelsOf(input), numSamples);
    }

    auto numOutputs =
        std::min(layout.outputs.size(), static_cast<int>(data.audio_outputs_count));

    for (auto bus = 0; bus < numOutputs; ++bus)
    {
        const auto& output = data.audio_outputs[bus];
        const float* const* matching = nullptr;
        auto matchingChannels = 0;

        if (bus < static_cast<int>(data.audio_inputs_count))
        {
            matching = data.audio_inputs[bus].data32;
            matchingChannels = channelsOf(data.audio_inputs[bus]);
        }

        pluginWrapper.bindOutput(bus,
                                 output.data32,
                                 channelsOf(output),
                                 numSamples,
                                 matching,
                                 matchingChannels);
    }
}

const void* Adapter::extension(const char* id) noexcept
{
    if (is(id, CLAP_EXT_PARAMS))
        return paramsExtension();

    if (is(id, CLAP_EXT_STATE))
        return stateExtension();

    if (is(id, CLAP_EXT_STATE_CONTEXT))
        return stateContextExtension();

    if (is(id, CLAP_EXT_AUDIO_PORTS))
        return audioPortsExtension();

    if (is(id, CLAP_EXT_NOTE_PORTS))
        return notePortsExtension();

    if (is(id, CLAP_EXT_LATENCY))
        return &latencyExtension;

    if (is(id, CLAP_EXT_TAIL))
        return &tailExtension;

    if (is(id, CLAP_EXT_RENDER))
        return &renderExtension;

    if (is(id, CLAP_EXT_TRACK_INFO) || is(id, CLAP_EXT_TRACK_INFO_COMPAT))
        return &trackInfoExtension;

    if (const auto* loop = hostLoopExtension(id))
        return loop;

    // No editor at all: no gui, and the host shows its own parameter page.
    if (is(id, CLAP_EXT_GUI) && plugin().hasEditor())
        return guiExtension();

    return nullptr;
}

void Adapter::onMainThread() noexcept
{
    if (latencyCheckPending.exchange(false))
        checkLatency();

    if (flushOnMainThread.exchange(false) && editsPending.load()
        && hostParams != nullptr)
        hostParams->request_flush(host);
}

uint32_t Adapter::latency() noexcept
{
    auto current = std::max(0, plugin().latencySamples());
    knownLatency = current;
    return static_cast<uint32_t>(current);
}

uint32_t Adapter::tail() const noexcept
{
    auto samples = pluginWrapper.plugin().tailSamples();

    if (samples == Plugin::infiniteTail)
        return std::numeric_limits<uint32_t>::max();

    return static_cast<uint32_t>(std::max(0, samples));
}

bool Adapter::setRenderMode(clap_plugin_render_mode mode) noexcept
{
    if (mode != CLAP_RENDER_REALTIME && mode != CLAP_RENDER_OFFLINE)
        return false;

    offline = mode == CLAP_RENDER_OFFLINE;
    return true;
}

void Adapter::trackInfoChanged() noexcept
{
    assert(isMainThread());

    try
    {
        readTrackInfo();
    }
    catch (...)
    {
    }
}

void Adapter::readTrackInfo()
{
    if (hostTrackInfo == nullptr)
        return;

    auto info = clap_track_info {};

    if (!hostTrackInfo->get(host, &info))
        return;

    auto track = TrackInfo {};
    track.isValid = true;

    if ((info.flags & CLAP_TRACK_INFO_HAS_TRACK_NAME) != 0)
        track.name = std::string(info.name, strnlen(info.name, sizeof(info.name)));

    if ((info.flags & CLAP_TRACK_INFO_HAS_TRACK_COLOR) != 0)
        track.color = {
            info.color.red, info.color.green, info.color.blue, info.color.alpha};

    pluginWrapper.setTrackInfo(std::move(track));
}

void Adapter::checkLatency() noexcept
{
    assert(isMainThread());

    pluginWrapper.takeLatencyChanged();

    // Inactive, the next activation tells the host (host_latency.changed).
    if (!active)
        return;

    auto current = std::max(0, plugin().latencySamples());
    auto known = knownLatency.load();

    if (known < 0 || known == current)
        return;

    // The baseline stays what the host knows, so the activation the restart
    // brings tells it; only the first caller restarts.
    if (!restartRequested.exchange(true) && host != nullptr)
        host->request_restart(host);
}

void Adapter::requestLatencyCheck() noexcept
{
    if (host != nullptr && !latencyCheckPending.exchange(true))
        host->request_callback(host);
}

void Adapter::takeAsBaseline(int index) noexcept
{
    lastPluginValue[static_cast<size_t>(index)].store(
        pluginWrapper.getNormalizedParameter(index), std::memory_order_relaxed);
}

void Adapter::takeAllAsBaseline() noexcept
{
    for (auto index: listed)
        takeAsBaseline(index);
}

bool Adapter::isListed(int index) const noexcept
{
    return pluginWrapper.plugin().parameters().isListedInCLAP(index);
}

void Adapter::markEditsPending() noexcept
{
    editsPending.store(true);

    // While processing, the next block sends it; otherwise the host flushes.
    if (!processing.load() && hostParams != nullptr)
        hostParams->request_flush(host);
}

void Adapter::beginParameterEdit(int index) noexcept
{
    pluginWrapper.holdParameter(index);

    if (!isListed(index))
        return;

    // Only the outermost of nested gestures reaches the host.
    if (editSlots[static_cast<size_t>(index)].depth++ == 0)
    {
        editSlots[static_cast<size_t>(index)].begins.fetch_add(1);
        markEditsPending();
    }
}

void Adapter::performParameterEdit(int index, float normalized) noexcept
{
    if (isListed(index))
    {
        auto& slot = editSlots[static_cast<size_t>(index)];
        lastPluginValue[static_cast<size_t>(index)].store(normalized,
                                                          std::memory_order_relaxed);
        slot.value.store(normalized, std::memory_order_relaxed);
        slot.dirty.store(true);
        markEditsPending();
    }

    checkLatency();
}

void Adapter::endParameterEdit(int index) noexcept
{
    if (isListed(index))
    {
        auto& slot = editSlots[static_cast<size_t>(index)];

        if (slot.depth > 0 && --slot.depth == 0)
        {
            slot.ends.fetch_add(1);
            markEditsPending();
        }
    }

    pluginWrapper.releaseParameter(index);
}

void Adapter::latencyChanged() noexcept
{
    checkLatency();
}

bool Adapter::mappingsChanged() const noexcept
{
    const auto& list = pluginWrapper.plugin().parameters();
    const auto space = list.identity().clapValues;

    for (auto i = 0; i < list.size(); ++i)
    {
        auto now = valueMappingFor(list[i], space);
        const auto& given = mappings[i];

        if (now.plain != given.plain || now.stepped != given.stepped
            || now.isEnum != given.isEnum || now.minValue != given.minValue
            || now.maxValue != given.maxValue)
            return true;
    }

    return false;
}

void Adapter::rebuildMappings() noexcept
{
    const auto& list = pluginWrapper.plugin().parameters();
    const auto space = list.identity().clapValues;

    for (auto i = 0; i < list.size(); ++i)
        mappings[i] = valueMappingFor(list[i], space);
}

// A range or steppedness the host was given can change only through a full
// rescan, which CLAP allows only while inactive: now when inactive, otherwise
// after the restart this asks for, at deactivation.
void Adapter::parameterInfoChanged() noexcept
{
    assert(isMainThread());

    if (mappingsChanged())
    {
        if (!active)
        {
            rebuildMappings();

            if (hostParams != nullptr)
                hostParams->rescan(host, CLAP_PARAM_RESCAN_ALL);

            return;
        }

        if (!std::exchange(mappingsStale, true) && host != nullptr)
            host->request_restart(host);
    }

    if (hostParams != nullptr)
        hostParams->rescan(host, CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT);
}

void Adapter::parameterValuesChanged() noexcept
{
    takeAllAsBaseline();

    if (hostParams != nullptr)
        hostParams->rescan(host, CLAP_PARAM_RESCAN_VALUES);

    checkLatency();
}

void Adapter::stateChanged() noexcept
{
    if (hostState != nullptr)
        hostState->mark_dirty(host);
}

} // namespace MakeASound::CLAP
