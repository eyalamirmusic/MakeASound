#pragma once

#include "../Core/Plugin.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace MakeASound
{

// The per-block pipeline every format adapter shares; a bad index is a no-op.
// In order: parameters, setPlayhead and setOffline, clearMidi/pushMidiIn/sortMidiInByOffset,
//   bindInput/bindOutput per bus, process(), drain midiOut().
//
// An effect that declares no bypass parameter gets one here (under a JUCE
// IdScheme, any plugin, instruments included, as JUCE's wrappers do): a
// BoolParam "Bypass" (id `__bypass`) appended to its parameters() as the bypass
// entry, so every format's host bypass reaches it. While it is on, process()
// runs the plugin's processBypassed() instead, with a 10 ms crossfade either way,
// and a Session document carries it as the top-level key `__bypass`.
class PluginWrapper
{
public:
    // The key the added bypass is saved under in a Session document.
    static constexpr const char* bypassStateKey = "__bypass";

    // `identity` is the plugin's description's (parameterIdentityFor()): every
    // entry of parameters() then carries the ids that scheme gives it, the added
    // bypass's included.
    explicit PluginWrapper(OwningPointer<Plugin> pluginToUse,
                           PluginFormat format = PluginFormat::Unknown,
                           ParameterIdentity identity = {});

    // Whether a wrapper around `plugin` under `identity` adds a bypass: it has
    // none of its own, wants one (Plugin::wantsHostBypass), and either its
    // declared layout has a main input or the identity gives one to every plugin
    // (AddedBypassFor::AllPlugins, a JUCE scheme's: JUCE's VST3 and AAX wrappers
    // give instruments one too). For code that describes a plugin without
    // wrapping it, as the AAX description does, with its description's identity.
    static bool addsBypass(const Plugin& plugin, const ParameterIdentity& identity);

    // Message thread only: a deferred load running there holds the plugin.
    ~PluginWrapper();

    PluginWrapper(const PluginWrapper&) = delete;
    PluginWrapper& operator=(const PluginWrapper&) = delete;

    Plugin& plugin() noexcept { return *pluginPtr; }
    const Plugin& plugin() const noexcept { return *pluginPtr; }

    // Sampled from the plugin at construction, replaced by setLayout().
    const BusLayout& busLayout() const noexcept { return layout; }

    // Host thread. Adopts the layout and resizes the context when the plugin
    // accepts it; false and nothing changes otherwise.
    bool setLayout(const BusLayout& proposed);

    // Host thread. The rate is the host's own, unrounded: it reaches the plugin's
    // ProcessSpec as given.
    void prepare(double sampleRate, int maxBlockSize);

    // Audio thread, after a gap in the stream.
    void reset() noexcept;

    // Message thread, from every adapter's latency check, in place of the
    // plugin's own takeLatencyChanged(): that, and the added bypass's delay grown
    // to the current latencySamples(), whatever moved it.
    bool takeLatencyChanged() noexcept;

    // saveState off the message thread reads the plugin's snapshot, and waits on
    // the message thread only for a plugin with none. loadState never waits: the
    // parameters land now, the rest later unless a newer load or teardown came.
    // With the added bypass, a Session save adds `__bypass` to a JSON object
    // document (an empty one becomes `{"__bypass": ...}`); a load takes it back
    // out before the plugin sees the document, and a Session load without it
    // turns the bypass off. A Preset never carries it.
    std::string saveState(StateContext context);
    void loadState(std::string_view data, StateContext context);

    // A document another framework wrote, for the plugin's loadForeignState: on
    // the message thread now, returning the plugin's answer; from any other
    // thread queued there with a copy of the bytes, ordered against loadState as
    // a load of its own (a later load of either kind wins, teardown drops it),
    // returning true and telling the host every value moved once the plugin took
    // it. A `bypass` in a Session state the plugin took sets the added bypass,
    // where there is one; a plugin with its own bypass reads it from the state.
    // A TamberPlugAU document the plugin declines loads as a native one where
    // the identity says so (ParameterIdentity::foreignPlugStateIsNative, the
    // TamberPlug profile's): Plug's schema is this framework's.
    bool loadForeignState(const ForeignState& state);

    // What the host said about the track: the plugin's trackInfo() and then its
    // onTrackInfoChanged(), now on the message thread, queued there from any
    // other (never the audio thread: queuing allocates), and dropped by teardown.
    void setTrackInfo(TrackInfo info);

    // The host application's name, for the plugin's hostName(). Message thread,
    // before setLayout() and prepare(), so the plugin can negotiate by it.
    void setHostName(std::string name);

    // Host writes; each is dropped while the parameter is held.
    void setNormalizedParameter(int index, float normalized) noexcept;
    void setParameter(int index, float plain) noexcept;
    float getParameter(int index) const noexcept;
    float getNormalizedParameter(int index) const noexcept;
    void setParameterByHostId(uint32_t hostId, float normalized) noexcept;

    // Held while a plugin-side gesture is open (see HostEditListener). Counted,
    // so a nested gesture cannot release its parent's hold.
    void holdParameter(int index) noexcept;
    void releaseParameter(int index) noexcept;
    bool isParameterHeld(int index) const noexcept;

    void setPlayhead(const Playhead& playhead) noexcept;

    // Audio thread, beside setPlayhead: the block's ProcessContext::isOffline.
    // It stays as set until set again.
    void setOffline(bool offline) noexcept;

    void clearMidi() noexcept;

    // False when the bus is unknown or full: the event is dropped rather than
    // growing the buffer on the audio thread.
    bool pushMidiIn(int bus, const MIDI::Event& event) noexcept;

    // Stable, so an adapter merging several host sources keeps a note-off ahead
    // of a note-on at the same offset.
    void sortMidiInByOffset() noexcept;

    const Vector<MIDI::Buffer>& midiOut() const noexcept { return context.midiOut; }

    // Host input may be const memory; the plugin reads it through a const Buffer.
    void bindInput(int bus,
                   const float* const* channels,
                   int numChannels,
                   int numSamples) noexcept;

    // Pre-populates the output so the plugin can work in place: a channel with a
    // matching input channel gets a copy of it, or nothing when the host aliased
    // the two, and a channel past the input's width is zeroed. Per channel, not
    // per bus: zeroing an output that aliases an input would wipe that input.
    void bindOutput(int bus,
                    float* const* channels,
                    int numChannels,
                    int numSamples,
                    const float* const* matchingInput = nullptr,
                    int matchingInputChannels = 0) noexcept;

    // Clears the MIDI out buses, runs the plugin with denormals flushed, then
    // detaches every audio bus so no host pointer outlives its block. With the
    // added bypass on, processBypassed() runs instead; across a toggle both run
    // for the crossfade, the bypassed one on a copy of the inputs, and the MIDI
    // out is the side being faded to.
    void process() noexcept;

private:
    struct Live
    {
        Plugin* plugin = nullptr;
        std::atomic<uint64_t> latestLoad {0};
    };

    // A host binding set aside while the crossfade's bypassed run borrows the bus.
    struct Binding
    {
        float* const* channels = nullptr;
        int numChannels = 0;
        int numSamples = 0;
        int startSample = 0;
    };

    static int addBypassAndCount(Plugin& plugin, ParameterIdentity identity);

    Parameter* parameterAt(int index) const noexcept;
    Parameter* writableParameterAt(int index) const noexcept;

    std::string savePluginState(StateContext context);
    static bool applyForeignState(Plugin& plugin, const ForeignState& state);
    std::string withBypass(std::string document, StateContext context) const;
    std::string_view takeBypass(std::string_view document,
                                StateContext context,
                                std::string& stripped);

    void prepareBypass(double sampleRate, int maxBlockSize);
    void processBypassable(const BoolParam& bypass) noexcept;
    bool canCrossfade() const noexcept;
    void crossfade(float target) noexcept;
    void runBypassedOnCopies() noexcept;
    void mixTowards(float target) noexcept;

    OwningPointer<Plugin> pluginPtr;
    BusLayout layout;
    ProcessContext context;
    EA::FixedDynamicArray<std::atomic<int>> holdCounts;

    // The added bypass's audio-thread state: 0 processing, 1 bypassed, between
    // while a crossfade runs, and the scratch the crossfade runs on, sized in
    // prepare().
    float bypassMix = 0.f;
    float fadeStep = 1.f;
    int fadeCapacity = 0;
    Vector<Buffer> fadeInputs;
    Vector<Buffer> fadeOutputs;
    Vector<Binding> hostInputs;
    Vector<Binding> hostOutputs;

    // How loadState's deferred half reaches the plugin. Declared last so it
    // expires before the plugin goes, and a queued load drops.
    std::shared_ptr<Live> live;
};

} // namespace MakeASound
