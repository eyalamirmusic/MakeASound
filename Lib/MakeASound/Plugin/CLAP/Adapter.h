#pragma once

#include "Gui.h"
#include "Params.h"
#include "Posix.h"
#include "../Host/PluginWrapper.h"

#include <clap/clap.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace MakeASound::CLAP
{

// One CLAP plugin instance over a PluginWrapper: the clap_plugin a host holds,
// whose calls become the wrapper's per-block steps, and the extensions it
// answers (params, state and state-context, audio and note ports, latency, tail,
// render, track-info, gui where the UI is built and the plugin has an editor,
// and on Linux posix-fd-support or timer-support for the message loop). The
// plugin's own edits leave through
// HostEditListener on the message thread and reach the host from the audio
// thread, as gesture and value events out of the next process() or
// params.flush(), which request_flush() asks for while the host is not
// processing (from on_main_thread when processing stopped with edits waiting).
// A gesture's begin goes out once however deeply it nests, its end once the
// outermost closes. Every id comes from the description's identity
// (ParameterList::Entry::clapId, ClapValueSpace), never from a profile.
class Adapter : public HostEditListener
{
public:
    // Installs itself as the plugin's HostEditListener; the destructor removes
    // it. `descriptor` is the factory's, which outlives the instance; `identity`
    // the description's (parameterIdentityFor).
    Adapter(const clap_host* host,
            const clap_plugin_descriptor* descriptor,
            OwningPointer<Plugin> plugin,
            ParameterIdentity identity = {});
    ~Adapter() override;

    Adapter(const Adapter&) = delete;
    Adapter& operator=(const Adapter&) = delete;

    // The object handed to the host: plugin_data is this adapter, destroy()
    // deletes it.
    const clap_plugin* clapPlugin() const noexcept { return &clap; }
    static Adapter* from(const clap_plugin* plugin) noexcept;

    Plugin& plugin() noexcept;
    PluginWrapper& wrapper() noexcept;

    const clap_host* clapHost() const noexcept { return host; }

    // clap.gui's view (Gui.cpp), from create to destroy; null otherwise. The
    // adapter's destructor drops one the host left, before the plugin goes.
    GuiViewPointer& guiView() noexcept { return gui; }

    // clap_plugin. init() adopts the host's main thread, reads its extensions,
    // its name (the plugin's hostName()) and its track info.
    bool init() noexcept;
    bool
        activate(double sampleRate, uint32_t minFrames, uint32_t maxFrames) noexcept;
    void deactivate() noexcept;
    bool startProcessing() noexcept;
    void stopProcessing() noexcept;
    void reset() noexcept;
    clap_process_status process(const clap_process& data) noexcept;
    const void* extension(const char* id) noexcept;
    void onMainThread() noexcept;

    // clap.params (Params.cpp). Positions count the listed entries in
    // declaration order (ParameterList::isListedInCLAP).
    int paramCount() const noexcept;
    bool paramInfo(int position, clap_param_info& info) const noexcept;
    bool paramValue(clap_id id, double& value) const noexcept;
    bool valueToText(clap_id id,
                     double value,
                     char* buffer,
                     uint32_t capacity) const noexcept;
    bool textToValue(clap_id id, const char* text, double& value) const noexcept;

    // [active ? audio thread : main thread], never during process(): the
    // host's values land now, and the plugin's own edits and moves go out.
    void flush(const clap_input_events* in, const clap_output_events* out) noexcept;

    // clap.state and clap.state-context (State.cpp), main thread.
    bool saveState(const clap_ostream& stream, StateContext context) noexcept;
    bool loadState(const clap_istream& stream, StateContext context) noexcept;

    // clap.latency: what the host now knows, the base of any later restart.
    uint32_t latency() noexcept;

    // clap.tail: UINT32_MAX for Plugin::infiniteTail.
    uint32_t tail() const noexcept;

    // clap.render: offline reaches ProcessContext::isOffline from the next block.
    bool setRenderMode(clap_plugin_render_mode mode) noexcept;

    // clap.track-info: the host's track info, read now into trackInfo().
    void trackInfoChanged() noexcept;

    // HostEditListener, message thread
    void beginParameterEdit(int index) noexcept override;
    void performParameterEdit(int index, float normalized) noexcept override;
    void endParameterEdit(int index) noexcept override;
    void latencyChanged() noexcept override;
    void parameterInfoChanged() noexcept override;
    void parameterValuesChanged() noexcept override;
    void stateChanged() noexcept override;

private:
    struct ClapSlot
    {
        clap_id id = 0;
        int index = 0;
    };

    // One listed parameter's plugin-side edits on their way from the message
    // thread to the host, preallocated per plugin index, so no edit is ever
    // dropped for want of room: the latest value (coalesced) with its dirty
    // flag, and counts of the gestures begun and ended (a nested gesture is one).
    // The audio side counts what the host was sent, so a gesture whose begin or
    // end the host's list refused goes out at the next drain instead.
    struct EditSlot
    {
        std::atomic<float> value {0.f};
        std::atomic<bool> dirty {false};
        std::atomic<uint32_t> begins {0};
        std::atomic<uint32_t> ends {0};

        // The drain's (the audio thread, or the main thread while inactive).
        uint32_t sentBegins = 0;
        uint32_t sentEnds = 0;

        // Message thread: the open gesture's nesting.
        int depth = 0;
    };

    // The plugin index of a listed clap_id, -1 for none; allocation-free.
    int indexOfClapId(clap_id id) const noexcept;

    // Events.cpp: the host's events, parameters pushed to the wrapper, then
    // notes and MIDI to its MIDI input, in the wrapper's documented order.
    void collectParameters(const clap_input_events& in, int numSamples) noexcept;
    void collectMidi(const clap_input_events& in, int numSamples) noexcept;
    void pushParameterValue(const clap_event_param_value& event,
                            int numSamples) noexcept;
    void emitEdits(const clap_output_events& out) noexcept;
    bool emitEditsOf(const clap_output_events& out, int index) noexcept;
    bool emitEditValueOf(const clap_output_events& out, int index) noexcept;
    void emitPluginMoves(const clap_output_events& out) noexcept;
    void emitMidi(const clap_output_events& out, int numSamples) noexcept;

    void bindBuses(const clap_process& data) noexcept;
    void silenceOutputs(const clap_process& data) noexcept;

    bool isListed(int index) const noexcept;

    // Message thread: an edit is waiting; while processing the next block sends
    // it, otherwise the host is asked to flush.
    void markEditsPending() noexcept;

    void takeAsBaseline(int index) noexcept;
    void takeAllAsBaseline() noexcept;

    // Main thread: the mappings by the parameters' current ranges, whether they
    // differ from those the host was given, and their rebuild (inactive only).
    bool mappingsChanged() const noexcept;
    void rebuildMappings() noexcept;

    // Main thread: the latency against what the host knows, a restart for a
    // change while active (once until the next activation tells the host).
    void checkLatency() noexcept;

    // Any thread but the audio one's caller: has the host call on_main_thread.
    void requestLatencyCheck() noexcept;

    void readTrackInfo();
    bool isMainThread() const noexcept;
    bool isAudioThread() const noexcept;

    template <typename Extension>
    const Extension* hostExtension(const char* id) const noexcept;

    const clap_host* host;
    clap_plugin clap {};
    PluginWrapper pluginWrapper;

    const clap_host_params* hostParams = nullptr;
    const clap_host_state* hostState = nullptr;
    const clap_host_latency* hostLatency = nullptr;
    const clap_host_thread_check* threadCheck = nullptr;
    const clap_host_track_info* hostTrackInfo = nullptr;

    // The listed plugin indices in declaration order; the same by clap_id,
    // sorted; each parameter's mapping, by plugin index; and the normalized
    // value each held when the host last heard of it.
    Vector<int> listed;
    Vector<ClapSlot> byClapId;
    Vector<ValueMapping> mappings;
    std::unique_ptr<std::atomic<float>[]> lastPluginValue;

    // Per MIDI output bus, the next event to send, for the merge by time.
    Vector<int> midiCursors;

    // By plugin index; any slot with something to send sets editsPending.
    std::unique_ptr<EditSlot[]> editSlots;
    std::atomic<bool> editsPending {false};

    // stop_processing found edits waiting: on_main_thread asks for a flush.
    std::atomic<bool> flushOnMainThread {false};

    // A range or steppedness moved while active: rebuilt at deactivation.
    bool mappingsStale = false;

    HostLoop hostLoop;

    GuiViewPointer gui;

    std::atomic<bool> processing {false};
    std::atomic<bool> offline {false};
    std::atomic<bool> latencyCheckPending {false};
    std::atomic<bool> active {false};
    double sampleRate = 48000.0;

    // A process() before the first activation renders silence.
    bool prepared = false;

    // What the host last read or was told during an activation; -1 before it
    // knew any. A change while active asks for one restart, flagged until the
    // next activation tells the host (host_latency.changed).
    std::atomic<int> knownLatency {-1};
    std::atomic<bool> restartRequested {false};
};

} // namespace MakeASound::CLAP
