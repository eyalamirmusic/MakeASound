#pragma once

// macOS only: an Audio Unit loaded and running in this process, over the
// AudioUnit C API, for a v2 component or a v3 extension through it. Everything
// format-neutral goes through HostedPlugin; this header is for code that must
// reach the unit itself (a hosted editor, a test).

#include "../HostedPlugin.h"
#include "../../Plugin/Parameters/ParameterGroup.h"

#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioUnitUtilities.h>
#include <CoreMIDI/CoreMIDI.h>

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace MakeASound::Hosting
{

class HostedAudioUnit;

// One parameter of a hosted Audio Unit, in the unit's own plain range, with its
// value strings for an indexed one. Its id is the AudioUnitParameterID in decimal
// and its host id that id itself (Global scope, element 0).
class HostedAudioUnitParameter : public Parameter
{
public:
    HostedAudioUnitParameter(HostedAudioUnit& ownerToUse,
                             AudioUnitParameterID idToUse,
                             const AudioUnitParameterInfo& info,
                             std::string nameToUse,
                             std::string unitsToUse,
                             std::vector<std::string> valueStringsToUse);

    AudioUnitParameterID parameterId() const noexcept { return auId; }

    float minValue() const noexcept override { return low; }
    float maxValue() const noexcept override { return high; }
    float defaultValue() const noexcept override { return initial; }
    int numSteps() const noexcept override { return steps; }
    bool isDiscrete() const noexcept override { return !valueStrings.empty(); }
    bool isBoolean() const noexcept override { return boolean; }

    // A host-side edit, off the audio thread: kept, then set on the unit through
    // the event listener, so the unit's own view hears it.
    void setValue(float plain) noexcept override;
    float getValue() const noexcept override;

    std::string valueToText(float plain) const override;
    float textToValue(std::string_view text) const override;

    // A value the unit reported, kept and sent nowhere. Any thread.
    void store(float plain) noexcept;

    // Re-reads the value from the unit, which its own view can move without
    // telling anyone. Message thread.
    void refreshFromUnit() noexcept;

    // Re-reads the name, units and value strings after a ParameterInfo change.
    // True when one moved. Message thread.
    bool updateInfo(std::string nameToUse,
                    std::string unitsToUse,
                    std::vector<std::string> valueStringsToUse);

    void markScheduled() noexcept;
    bool takeScheduled() noexcept;

private:
    int indexOf(float plain) const noexcept;

    HostedAudioUnit& owner;
    AudioUnitParameterID auId;
    std::vector<std::string> valueStrings;
    float low;
    float high;
    float initial;
    int steps;
    bool boolean;
    std::atomic<float> value;
    std::atomic<bool> scheduled {false};
};

class HostedAudioUnit : public HostedPlugin
{
public:
    // Takes the instance, which it disposes.
    HostedAudioUnit(PluginDescription description,
                    AudioComponentInstance instanceToUse,
                    AudioComponentDescription componentToUse);

    ~HostedAudioUnit() override;

    BusLayout getBusLayout() const override { return layout; }
    bool acceptsLayout(const BusLayout& proposed) const override;

    void prepare(const ProcessSpec& spec) override;
    void process(ProcessContext& context) noexcept override;

    int latencySamples() const noexcept override;
    int tailSamples() const noexcept override;

    // kAudioUnitProperty_ClassInfo as a binary property list.
    std::string saveState(StateContext context) override;
    void loadState(std::string_view data, StateContext context) override;

    bool setParameterFromAudioThread(int index, float normalized) noexcept override;
    void syncEditorWithProcessor() override;
    void refreshParameterValue(int index) override;

    // JUCE's AU host listed every parameter whose info reads, the ones neither
    // readable nor writable included (parameters() leaves those out), with a
    // clump's gathered where the clump first appears. -1 for one left out here,
    // or past the end.
    int indexForControllerIndex(int32_t controllerIndex) const noexcept override;

    // At most this many audio-thread values per block; past it, false.
    static constexpr int scheduledCapacity = 256;

    AudioUnit audioUnit() const noexcept { return instance; }
    const AudioComponentDescription& component() const noexcept { return described; }
    bool isInitialized() const noexcept { return initialized; }

    // The parameter carrying `id`, or -1.
    int indexOfParameterId(AudioUnitParameterID id) const noexcept;

    // Message thread, from the parameters.
    void parameterEdited(AudioUnitParameterID id, float plain);

private:
    struct HostedGroup : ParameterGroup
    {
        void addAll(Vector<OwningPointer<HostedAudioUnitParameter>>& params)
        {
            add(params);
        }
    };

    void readBusLayout();
    std::vector<AudioUnitParameterID> readParameterIds() const;
    void buildParameters();
    void installListeners();
    void removeListeners() noexcept;
    void readLatencyAndTail() noexcept;
    bool applyStreamFormat(AudioUnitScope scope, int element, int channels) const;
    bool refreshInfo();
    void refreshInfoIfDirty();
    void refreshValues() noexcept;
    void signalMainLoop() noexcept;

    HostedAudioUnitParameter* findParameter(AudioUnitParameterID id) const noexcept;
    void parameterEvent(const AudioUnitEvent& event, AudioUnitParameterValue value);

    void pushMidi(const ProcessContext& context) noexcept;
    void scheduleParameters() noexcept;
    OSStatus supplyInput(int bus, UInt32 numFrames, AudioBufferList* data) noexcept;
    AudioBufferList* outputList(int bus) noexcept;

    static OSStatus renderInput(void* self,
                                AudioUnitRenderActionFlags* flags,
                                const AudioTimeStamp* time,
                                UInt32 bus,
                                UInt32 numFrames,
                                AudioBufferList* data);
    static void propertyChanged(void* self,
                                AudioUnit unit,
                                AudioUnitPropertyID property,
                                AudioUnitScope scope,
                                AudioUnitElement element);
    static void eventArrived(void* self,
                             void* object,
                             const AudioUnitEvent* event,
                             UInt64 hostTime,
                             AudioUnitParameterValue value);
    static void onMainLoop(void* self);

    // The unit's MIDI out, called from inside AudioUnitRender: each packet's
    // time stamp is its sample offset in the block.
    static OSStatus midiOutput(void* self,
                               const AudioTimeStamp* time,
                               UInt32 bus,
                               const MIDIPacketList* packets);
    void receiveMidi(int bus, const MIDIPacketList& packets) noexcept;

    static OSStatus beatAndTempo(void* self, Float64* beat, Float64* tempo);
    static OSStatus musicalTime(void* self,
                                UInt32* toNextBeat,
                                Float32* numerator,
                                UInt32* denominator,
                                Float64* downBeat);
    static OSStatus transportState(void* self,
                                   Boolean* playing,
                                   Boolean* recording,
                                   Boolean* changed,
                                   Float64* sample,
                                   Boolean* cycling,
                                   Float64* cycleStart,
                                   Float64* cycleEnd);
    static OSStatus transportStateV1(void* self,
                                     Boolean* playing,
                                     Boolean* changed,
                                     Float64* sample,
                                     Boolean* cycling,
                                     Float64* cycleStart,
                                     Float64* cycleEnd);

    AudioComponentInstance instance = nullptr;
    AudioComponentDescription described {};
    BusLayout layout;
    bool consumesMidi = false;

    Vector<OwningPointer<HostedAudioUnitParameter>> storage;
    HostedGroup group;
    Vector<HostedAudioUnitParameter*> byId;

    // Every id the unit listed when loaded, sorted, the ones left out of
    // parameters() included: a list that differs is a changed set.
    std::vector<AudioUnitParameterID> listedIds;

    // Per parameter in JUCE's order, its storage index or -1.
    Vector<int> juceIndices;

    AUEventListenerRef eventListener = nullptr;
    bool listening = false;

    // A main-run-loop source a property listener signals from any thread, so a
    // rename is read back, and a latency change told to the listener, on the
    // message thread.
    CFRunLoopRef mainRunLoop = nullptr;
    std::atomic<CFRunLoopSourceRef> mainLoopSource {nullptr};
    std::atomic<bool> infoDirty {false};
    std::atomic<bool> latencyDirty {false};

    // Sized in prepare(): one AudioBufferList per output bus, and the stand-ins
    // for channels the caller did not supply.
    std::vector<std::vector<std::uint64_t>> outputLists;
    std::vector<float> silence;
    std::vector<float> scratch;

    // The block being rendered, for the input pull and the MIDI the unit sends.
    ProcessContext* currentContext = nullptr;
    Playhead blockPlayhead;

    std::array<AudioUnitParameterEvent, scheduledCapacity> scheduledEvents {};
    int scheduledCount = 0;
    std::atomic<bool> anyScheduled {false};

    double sampleRate = 44100.0;
    int maxBlockSize = 0;
    int64_t renderSampleTime = 0;

    std::atomic<int> latency {0};
    std::atomic<double> tail {0.0};
    bool initialized = false;
};

// Loads `component`, already found or registered, as the plugin `description`
// names: a v2 instantiated directly, a v3 out of process first and in process if
// that fails, each with a timeout. What AUFormat::load does once it has found the
// component; public so a test can hand over a component it registered itself.
LoadResult loadAudioUnit(AudioComponent component,
                         PluginDescription description,
                         const HostInfo& host);

} // namespace MakeASound::Hosting
