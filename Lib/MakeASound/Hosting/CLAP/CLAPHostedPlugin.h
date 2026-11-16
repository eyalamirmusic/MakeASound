#pragma once

// CLAP-only: the hosted plugin's C API side, for code that has to reach the
// plugin itself (a hosted editor, a test standing in for a host's GUI). It
// includes the CLAP headers; everything else goes through HostedPlugin.

#include "../HostedPlugin.h"
#include "../../Realtime/SPSCQueue.h"
#include "CLAPEvents.h"
#include "CLAPFdWatches.h"
#include "CLAPHostedParameter.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/latency.h>
#include <clap/ext/log.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/posix-fd-support.h>
#include <clap/ext/state-context.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>
#include <clap/ext/thread-check.h>
#include <clap/ext/timer-support.h>
#include <clap/host.h>
#include <clap/plugin.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eacp::Threads
{
class Timer;
}

namespace MakeASound::Hosting
{

class CLAPModule;

// A loaded CLAP plugin: one clap_plugin, created against a clap_host of its own
// whose host_data is this object, init()ed, and driven through the spec's
// lifecycle. The module stays loaded while the plugin lives.
//
// Threads, as the spec names them:
//
//   main thread   the message thread (eacp's main thread), outside any call
//                 below that borrows the audio thread
//   audio thread  whichever thread is inside process(), or inside a call this
//                 host makes on the audio thread's behalf while it holds
//                 process() off (stop_processing before deactivate); a
//                 thread_local flag tells which
//
// prepare() deactivates if active, then activates (sample rate, 1, the most
// frames) and sizes every buffer and event list; start_processing() runs on the
// audio thread before the first block after each activation; stop_processing()
// and deactivate() run before the next activation and before destroy(). Any
// main-thread call that must not overlap a block (prepare, a restart, the
// destructor) first holds process() off: a block arriving meanwhile outputs
// silence.
//
// What the plugin asks for from another thread (request_callback,
// request_restart, request_flush) is flagged and run on the main thread by a
// timer of this instance, or at once by syncEditorWithProcessor(); a request
// made on the main thread itself is also posted to the message loop. Nothing is
// called into the plugin once it is destroyed.
//
// A host edit is queued for the next block. Inactive, no block comes, so it goes
// out at once through params.flush on the main thread. Active but idle (no block
// for a while, the host's transport stopped, say), a service holds blocks off and
// flushes as the audio thread, which also serves a request_flush no block took.
// An edit the full queue refuses is kept per parameter, its latest value sent
// once there is room. A state load discards whatever edits are still queued.
//
// The plugin's own parameter output (from a block or a flush) reaches the
// HostEditListener on the main thread, after the call that produced it: a
// gesture as begin, performs, end. A value outside any gesture is only kept in
// the parameter, as VST3's processor output is, and reaches no listener. A
// gesture whose begin finds the queue full is dropped whole, and an end always
// has room, so the listener never sees one half of a gesture.
class CLAPHostedPlugin
    : public HostedPlugin
    , private CLAPParameterSink
{
public:
    // Makes the plugin against `host`, as a factory's create_plugin does.
    using CreateFunction = std::function<const clap_plugin*(const clap_host* host)>;

    // A plugin made by `create` and init()ed, holding `module` (may be null for
    // a plugin not made from one): the plugin, or why there is none (create made
    // nothing, or init() refused, in which case it was destroyed). Message
    // thread.
    static LoadResult create(PluginDescription description,
                             std::shared_ptr<CLAPModule> module,
                             const CreateFunction& create,
                             const HostInfo& host);

    // Use create(): construction alone makes no plugin.
    CLAPHostedPlugin(PluginDescription description,
                     std::shared_ptr<CLAPModule> moduleToUse,
                     std::string hostNameToUse);

    ~CLAPHostedPlugin() override;

    BusLayout getBusLayout() const override { return layout; }

    // A CLAP plugin's ports are its own, channel counts included: only the
    // layout it declares is accepted.
    bool acceptsLayout(const BusLayout& proposed) const override;

    void prepare(const ProcessSpec& spec) override;
    void process(ProcessContext& context) noexcept override;

    int latencySamples() const noexcept override;

    // clap.tail, on the main thread or, from any other thread, as the audio
    // thread: a caller off the main thread must not overlap a block.
    int tailSamples() const noexcept override;

    // Whether the plugin offers clap.gui. No CLAP editor is made yet.
    bool hasEditor() const override { return gui != nullptr; }

    // clap.state, or clap.state-context where the plugin has it (a preset as
    // FOR_PRESET, a session as FOR_PROJECT). Message thread.
    std::string saveState(StateContext context) override;
    void loadState(std::string_view data, StateContext context) override;

    // A normalized value, linear over the parameter's plain range, sent in the
    // next block at its start.
    bool setParameterFromAudioThread(int index, float normalized) noexcept override;

    // A CLAP plugin's GUI and processor are one instance, so there is nothing to
    // push. Runs, now, whatever the plugin asked of the main thread (its
    // callback, a restart, a flush) and hands the gestures its blocks reported
    // to the HostEditListener.
    void syncEditorWithProcessor() override;

    // Re-reads the value through clap.params. Message thread.
    void refreshParameterValue(int index) override;

    const clap_plugin* clapPlugin() const noexcept { return plugin; }
    const clap_host* clapHost() const noexcept { return &host; }

    // The module the plugin was made from (shared by every plugin a CLAPFormat
    // loaded from it), or null.
    const std::shared_ptr<CLAPModule>& clapModule() const noexcept { return module; }

    // The plugin's clap.gui, or null.
    const clap_plugin_gui* guiExtension() const noexcept { return gui; }

    // The parameter carrying `id`, or -1.
    int indexOfParamId(clap_id id) const noexcept;

    // Activated and not yet deactivated. Message thread.
    bool isActive() const noexcept { return active; }

private:
    struct Callbacks;
    friend struct Callbacks;

    struct HostedGroup : ParameterGroup
    {
        void addAll(Vector<OwningPointer<CLAPHostedParameter>>& params)
        {
            add(params);
        }
    };

    // Message thread to audio thread: a host edit, plain, and the edit
    // generation it was made in (one before the last state load is stale).
    struct ParamEdit
    {
        clap_id id = CLAP_INVALID_ID;
        void* cookie = nullptr;
        double value = 0.0;
        uint32_t generation = 0;
    };

    // Audio thread to message thread: a gesture or a value inside one, from the
    // plugin's output events, for the HostEditListener.
    struct GestureEvent
    {
        enum Kind : uint8_t
        {
            begin,
            perform,
            end
        };

        Kind kind = begin;
        int index = -1;
        float normalized = 0.f;
    };

    // One note port's dialects, read when the ports are.
    struct NotePort
    {
        bool takesMidi = false;
        bool prefersClap = false;
    };

    // gestureOpen's states. A gesture whose begin found no room is open but
    // unreported: its values and its end are dropped with it.
    enum GestureState : uint8_t
    {
        gestureClosed,
        gestureReported,
        gestureUnreported
    };

    bool initialise(const CreateFunction& create, std::string& error);
    void setUpHost();
    void queryExtensions();

    template <typename Extension>
    const Extension* pluginExtension(const char* id) const noexcept;

    // Every port, in the plugin's own order: one get() refuses is kept as a
    // placeholder (no channels, no dialect) so the indices stay the plugin's.
    // False when an audio port was refused, since its channels are unknown: the
    // plugin is then not activated.
    bool readBusLayout();
    void readNotePorts();
    void buildParameters();
    void sizeBuffers();

    // Main thread: holds process() and setParameterFromAudioThread() off until
    // prepared again.
    void holdOffAudio() noexcept;

    bool activate();
    void deactivate();
    void restart();
    void readLatency();
    void updatePortNames();

    void parameterEdited(const CLAPHostedParameter& param,
                         double plain) noexcept override;
    void queueEdit(const CLAPHostedParameter& param, double plain) noexcept;
    void retryOverflowedEdits() noexcept;

    // Main thread, the audio thread excluded (inactive, or held off): the queued
    // edits through params.flush, as the audio thread when active. `requested`
    // flushes with nothing to send too. Not nested: an edit made from inside the
    // flush is queued and taken by its next pass.
    void flushPendingEdits(bool requested) noexcept;

    // Active, no block lately: blocks held off for a flush, then let in again.
    bool audioIdle() noexcept;
    void flushWhileIdle() noexcept;

    void serviceMainThread();
    void requestService() noexcept;
    void pushGesture(GestureEvent::Kind kind, int index, float normalized) noexcept;
    bool gestureRoomFor(int events) const noexcept;
    void drainGestures() noexcept;
    void dispatchGesture(const GestureEvent& event) noexcept;
    void onTimer(clap_id id);

    void rescanParameters(uint32_t flags);
    void refreshValues();
    bool refreshInfo();
    bool parameterSetChanged() const;

    CLAPHostedParameter* findParameter(clap_id id) const noexcept;

    static clap_event_param_value
        paramEventOf(clap_id id, void* cookie, double value, uint32_t time) noexcept;

    void bindAudio(ProcessContext& context, int numSamples) noexcept;
    void drainEdits() noexcept;
    void pushMidi(ProcessContext& context, int numSamples) noexcept;
    void fillTransport(const Playhead& playhead) noexcept;
    void readOutputEvents(ProcessContext& context, int numSamples) noexcept;

    // Whichever thread holds the plugin's processing side (the audio thread in a
    // block, the main thread in a flush), which is the gesture queue's producer
    // meanwhile; the main thread dispatches it later.
    void readOutputParameter(const clap_event_header& header) noexcept;

    // Declared first, so it is released last: nothing of the module may be
    // reached once it goes.
    std::shared_ptr<CLAPModule> module;

    std::string hostName;
    clap_host host {};
    clap_host_log hostLog {};
    clap_host_thread_check hostThreadCheck {};
    clap_host_params hostParams {};
    clap_host_state hostState {};
    clap_host_latency hostLatency {};
    clap_host_tail hostTail {};
    clap_host_audio_ports hostAudioPorts {};
    clap_host_note_ports hostNotePorts {};
    clap_host_timer_support hostTimers {};
    clap_host_posix_fd_support hostFds {};

    const clap_plugin* plugin = nullptr;

    // init() returned true, and everything it lets this host read is read.
    bool initialised = false;
    bool ready = false;

    const clap_plugin_params* params = nullptr;
    const clap_plugin_state* state = nullptr;
    const clap_plugin_state_context* stateContext = nullptr;
    const clap_plugin_audio_ports* audioPorts = nullptr;
    const clap_plugin_note_ports* notePorts = nullptr;
    const clap_plugin_latency* latency = nullptr;
    const clap_plugin_tail* tail = nullptr;
    const clap_plugin_gui* gui = nullptr;

    // Asked again when the plugin first registers a timer or a descriptor, which
    // it may do inside its init().
    const clap_plugin_timer_support* pluginTimers = nullptr;
    const clap_plugin_posix_fd_support* pluginFds = nullptr;

    BusLayout layout;
    std::vector<NotePort> notePortsIn;

    Vector<OwningPointer<CLAPHostedParameter>> storage;
    HostedGroup group;

    // By clap_id, so the audio thread resolves an id with a binary search.
    Vector<CLAPHostedParameter*> byId;

    // Per storage index: a GestureState. The audio thread's while active, the
    // main thread's (a flush) while blocks are held off or inactive, as is
    // `reportedOpen`, how many are gestureReported: the gesture queue always
    // keeps that much room, one end each.
    std::vector<uint8_t> gestureOpen;
    int reportedOpen = 0;

    // Per storage index, the audio thread's: where in inputEvents this block's
    // write of it from setParameterFromAudioThread is, valid while the stamp is
    // `blockSerial`, so a second write replaces the first without a search.
    std::vector<uint32_t> audioWriteStamp;
    std::vector<int> audioWriteSlot;
    uint32_t blockSerial = 1;

    // Per storage index, the main thread's: an edit the full queue refused,
    // sent (at the parameter's value then) once there is room.
    std::vector<uint8_t> editOverflow;
    bool overflowPending = false;

    // Sized in prepare() and only re-pointed per block. A channel the caller did
    // not supply reads `silence` or writes `scratch`: a CLAP plugin may touch
    // every channel of every port it declares.
    std::vector<clap_audio_buffer> inputBuffers;
    std::vector<clap_audio_buffer> outputBuffers;
    std::vector<std::vector<float*>> inputTables;
    std::vector<std::vector<float*>> outputTables;
    std::vector<float> silence;
    std::vector<float> scratch;

    // The audio thread's (and setParameterFromAudioThread's), sized in
    // prepare().
    CLAPInputEvents inputEvents;
    CLAPOutputEvents outputEvents;
    clap_event_transport transport {};

    // The main thread's, for a flush with blocks held off or inactive.
    CLAPInputEvents flushInput;
    CLAPOutputEvents flushOutput;

    // Produced by the main thread; consumed by the audio thread, or by the main
    // thread while blocks are held off or inactive.
    SPSCQueue<ParamEdit, 1024> pendingEdits;
    std::atomic<uint32_t> editGeneration {0};

    // Produced by whichever thread holds the processing side (see
    // readOutputParameter), consumed by the main thread.
    SPSCQueue<GestureEvent, 1024> gestures;

    double sampleRate = 0.0;
    int maxBlockSize = 0;
    int64_t steadyTime = 0;

    std::atomic<bool> prepared {false};
    std::atomic<bool> inAudioCall {false};
    std::atomic<bool> processing {false};
    std::atomic<int> latencyNow {0};
    bool latencyKnown = false;

    std::atomic<bool> callbackRequested {false};
    std::atomic<bool> restartRequested {false};
    std::atomic<bool> flushRequested {false};
    std::atomic<bool> gesturesPending {false};

    // Blocks run so far, and the main thread's view of it: the count it last
    // saw, when it saw it move, and how long without a block counts as idle.
    std::atomic<uint32_t> blocksRun {0};
    uint32_t blocksSeen = 0;
    std::chrono::steady_clock::time_point blocksSeenAt;
    std::chrono::steady_clock::duration idleAfter {};

    // Main thread.
    bool active = false;
    bool activating = false;
    bool loadingState = false;
    bool servicePosted = false;
    bool servicing = false;
    bool flushing = false;
    bool draining = false;
    bool editsWaiting = false;
    int timerDepth = 0;

    // clap.timer-support, by the id each was given; a timer unregistered (from
    // inside its own tick, possibly) is kept until a service runs outside every
    // tick (`timerDepth` counts the ticks on the stack: one may pump the loop).
    std::map<clap_id, std::unique_ptr<eacp::Threads::Timer>> timers;
    std::vector<std::unique_ptr<eacp::Threads::Timer>> retiredTimers;
    clap_id nextTimerId = 1;

    // clap.posix-fd-support, Linux only; null elsewhere.
    OwningPointer<CLAPFdWatches> fdWatches;

    // Runs serviceMainThread() a few dozen times a second.
    std::unique_ptr<eacp::Threads::Timer> serviceTimer;

    // Shared with what is posted to the message loop, which checks it is still
    // set before reaching this object.
    std::shared_ptr<CLAPHostedPlugin*> self;
};

} // namespace MakeASound::Hosting
