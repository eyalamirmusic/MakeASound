#pragma once

#include "Plugin.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace MakeASound
{

// The per-block pipeline every format adapter shares; a bad index is a no-op.
// In order: parameters, setPlayhead, clearMidi/pushMidiIn/sortMidiInByOffset,
//   bindInput/bindOutput per bus, process(), drain midiOut().
class PluginWrapper
{
public:
    explicit PluginWrapper(OwningPointer<Plugin> pluginToUse,
                           PluginFormat format = PluginFormat::Unknown);

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

    // Host thread.
    void prepare(int sampleRate, int maxBlockSize);

    // Audio thread, after a gap in the stream.
    void reset() noexcept;

    // saveState off the message thread reads the plugin's snapshot, and waits on
    // the message thread only for a plugin with none. loadState never waits: the
    // parameters land now, the rest later unless a newer load or teardown came.
    std::string saveState(StateContext context);
    void loadState(std::string_view data, StateContext context);

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
    // detaches every audio bus so no host pointer outlives its block.
    void process() noexcept;

private:
    struct Live
    {
        Plugin* plugin = nullptr;
        std::atomic<uint64_t> latestLoad {0};
    };

    Parameter* parameterAt(int index) const noexcept;
    Parameter* writableParameterAt(int index) const noexcept;

    OwningPointer<Plugin> pluginPtr;
    BusLayout layout;
    ProcessContext context;
    EA::FixedDynamicArray<std::atomic<int>> holdCounts;

    // How loadState's deferred half reaches the plugin. Declared last so it
    // expires before the plugin goes, and a queued load drops.
    std::shared_ptr<Live> live;
};

} // namespace MakeASound
