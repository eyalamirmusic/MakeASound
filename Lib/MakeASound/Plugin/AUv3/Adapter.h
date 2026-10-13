#pragma once

// Objective-C++ only: the module's AUAudioUnit subclass (AudioUnit.mm) owns one
// of these and forwards every override to it, so the format lives in the static
// library and the per-module class is a thin shell.

#include "../AU/HostParameters.h"
#include "../Core/Description.h"
#include "../Host/PluginWrapper.h"
#include "../../Realtime/SpinLock.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <functional>

namespace MakeASound::AUv3
{

// The AUv3 format: an AUAudioUnit's buses, parameter tree, state, latency and
// render block over a PluginWrapper. The instance finds its plugin by its own
// component's subtype, so several plugins share one extension.
class Adapter final : public HostEditListener
{
public:
    // Null, with outError set, when the description names no plugin of this
    // module. `unit` owns the adapter and is held weakly.
    static OwningPointer<Adapter> create(AUAudioUnit* unit,
                                         AudioComponentDescription description,
                                         NSError** outError);

    // Over the plugin `plugin` names rather than the one the description's codes
    // find in this module; its IdScheme says how the tree addresses the
    // parameters. Null, with outError set, when it creates no plugin. `plugin`
    // must outlive the adapter.
    static OwningPointer<Adapter> create(AUAudioUnit* unit,
                                         AudioComponentDescription description,
                                         const PluginDescription& plugin,
                                         NSError** outError);

    // Message thread, where the wrapper must go.
    ~Adapter() override;

    Adapter(const Adapter&) = delete;
    Adapter& operator=(const Adapter&) = delete;

    Plugin& plugin() noexcept;
    PluginWrapper& wrapper() noexcept;

    // A view the host keeps past the unit closes itself through these, which the
    // destructor runs first, while the wrapper still lives. Message thread.
    void addViewCloser(void* key, std::function<void()> closer);
    void removeViewCloser(void* key);

    AUAudioUnitBusArray* inputBusses() const { return inputs; }
    AUAudioUnitBusArray* outputBusses() const { return outputs; }

    // [in, out] pairs over 1..8 channels per main bus that acceptsLayout takes, 0
    // for a side with no bus, as the v2 adapter publishes them.
    NSArray<NSNumber*>* channelCapabilities() const { return capabilities; }

    // A bus judged alone, since hosts set one at a time: a main bus's count must
    // appear on its side of some published pair, any other bus keeps its declared
    // count; float32, non-interleaved, never while render resources are allocated.
    bool shouldChangeToFormat(AVAudioFormat* format, AUAudioUnitBus* bus) const;

    // After the base class's: setLayout from the buses' formats, prepare at the
    // first output's rate and maximumFramesToRender, size the tables and our own
    // buffers, capture the host's blocks.
    bool allocateRenderResources(NSError** outError);
    void deallocateRenderResources();

    AUInternalRenderBlock internalRenderBlock();

    AUParameterTree* parameterTree() const { return tree; }

    NSTimeInterval latency();
    NSTimeInterval tailTime() const;

    // Over the plugin's bypass parameter; false and a refused write without one.
    bool shouldBypassEffect() const noexcept;
    void setShouldBypassEffect(bool bypass);

    // The next render resets the plugin.
    void reset() noexcept;

    // fullState is a Preset, fullStateForDocument a Session.
    NSDictionary<NSString*, id>* fullState(StateContext context);
    void setFullState(NSDictionary<NSString*, id>* state, StateContext context);

    NSArray<NSString*>* midiOutputNames() const;

    // The unit's contextName, which a host sets to the track's name: the
    // plugin's trackInfo() name, nil an empty one. Any thread.
    void setContextName(NSString* name);

    // Every exposed parameter whose plugin value moved since the last call goes
    // into the tree, as does a held parameter's dropped host write being undone.
    // Message thread; a 30 Hz timer calls it, and so does a load.
    void syncTreeFromPlugin();

    // HostEditListener, message thread
    void beginParameterEdit(int index) noexcept override;
    void performParameterEdit(int index, float normalized) noexcept override;
    void endParameterEdit(int index) noexcept override;
    void latencyChanged() noexcept override;
    void parameterInfoChanged() noexcept override;
    void parameterValuesChanged() noexcept override;

    // stateChanged() keeps the default: an AUAudioUnit has no dirty signal.

private:
    struct Render;

    struct ViewCloser
    {
        void* key = nullptr;
        std::function<void()> close;
    };

    Adapter(AUAudioUnit* unit,
            AudioComponentDescription description,
            const PluginDescription& pluginDescription,
            OwningPointer<Plugin> plugin);

    void buildBusses();
    void buildCapabilities();
    void buildTree();
    static void clearTreeBlocks(AUParameterTree* old);
    void startTreeTimer();

    BusLayout negotiatedLayout() const;
    double outputSampleRate() const;
    bool isPublishedCount(bool input, int channels) const noexcept;
    // By the entry's 64-bit Entry::address (ParameterList::indexOfAddress), -1
    // for one the tree does not list. Allocation-free.
    int indexOfExposed(AUParameterAddress address) const noexcept;
    bool isListed(int index) const noexcept;
    int slotOf(int index) const noexcept;
    AUParameter* treeParameter(int index) const;

    // The plugin's value in the tree's space, the v2 info's (ParameterSpace).
    AUValue treeValueOf(int index) const noexcept;
    float plainFromTree(int index, AUValue value) const noexcept;
    void setTreeValue(
        int index,
        AUParameterAutomationEventType type = AUParameterAutomationEventTypeValue);
    void hostWrites(int index, float plain) noexcept;
    void notifyHostIfLatencyChanged();
    void notifyKeyChanged(NSString* key) const;

    AUAudioUnitStatus
        render(AudioUnitRenderActionFlags* flags,
               const AudioTimeStamp* timestamp,
               AUAudioFrameCount frames,
               NSInteger outputBus,
               AudioBufferList* output,
               const AURenderEvent* events,
               __unsafe_unretained AURenderPullInputBlock pullInput) noexcept;
    void renderBlock(AudioUnitRenderActionFlags* flags,
                     const AudioTimeStamp* timestamp,
                     AUAudioFrameCount frames,
                     AudioBufferList* singleOutput,
                     const AURenderEvent* events,
                     __unsafe_unretained AURenderPullInputBlock pullInput) noexcept;
    void bindInputs(const AudioTimeStamp* timestamp,
                    AUAudioFrameCount frames,
                    __unsafe_unretained AURenderPullInputBlock pullInput) noexcept;
    void applyEvents(const AURenderEvent* events,
                     const AudioTimeStamp* timestamp,
                     AUAudioFrameCount frames) noexcept;
    void bindOutputs(AUAudioFrameCount frames,
                     AudioBufferList* singleOutput) noexcept;
    void emitMidiOut(const AudioTimeStamp* timestamp) noexcept;

    __weak AUAudioUnit* unit = nil;
    AudioComponentDescription description {};

    PluginWrapper pluginWrapper;

    // How the tree sees the parameters, by the plugin description's IdScheme.
    AU::ParameterSpace space;

    AUAudioUnitBusArray* inputs = nil;
    AUAudioUnitBusArray* outputs = nil;
    NSArray<NSNumber*>* capabilities = nil;
    AUParameterTree* tree = nil;
    dispatch_source_t treeTimer = nil;

    // Indices of the listed entries in declaration order, and per entry what
    // the plugin held at the last sync and whether a dropped host write is to be
    // undone in the tree.
    Vector<int> exposed;
    EA::FixedDynamicArray<std::atomic<float>> lastPluginValue;
    EA::FixedDynamicArray<std::atomic<bool>> undoHostWrite;

    // What the render block captures: C++ only, sized at allocate.
    OwningPointer<Render> state;

    std::atomic<bool> resetPending {false};

    // What the host last fetched; -1 while it has no baseline to be told about.
    std::atomic<int> lastReportedLatency {-1};

    Vector<ViewCloser> viewClosers;
};

} // namespace MakeASound::AUv3
