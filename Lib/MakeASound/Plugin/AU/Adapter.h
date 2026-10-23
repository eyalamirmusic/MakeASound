#pragma once

#include "AUCommon.h"
#include "HostParameters.h"
#include "../Core/Description.h"
#include "../Host/PluginWrapper.h"
#include "../Realtime/RealtimeSwap.h"
#include "../../Realtime/SpinLock.h"

#include <AudioToolbox/AudioUnitUtilities.h>

#include <atomic>
#include <functional>
#include <memory>

namespace MakeASound::AU
{

// The one AU class every plugin ships as: a MusicDeviceBase, so one class answers
// the effect, music-effect, instrument and MIDI-processor selectors, over a
// PluginWrapper. The instance finds its plugin by its own component's subtype.
class Adapter
    : public au::MusicDeviceBase
    , public HostEditListener
{
public:
    // Throws when the component's subtype names no plugin of this module; the
    // SDK's Open catches it and fails the instantiation.
    explicit Adapter(AudioComponentInstance instance);

    // Over the plugin `description` names rather than the one the component's
    // subtype finds in this module; its IdScheme says how the host addresses the
    // parameters. Throws when it creates no plugin. `description` must outlive
    // the adapter.
    Adapter(AudioComponentInstance instance, const PluginDescription& description);
    ~Adapter() override;

    // The plugin this module ships with that subtype, or null.
    static const PluginDescription* describe(OSType subtype) noexcept;
    static OSType manufacturer() noexcept;

    Plugin& plugin() noexcept;
    PluginWrapper& wrapper() noexcept;

    // A view the host owns past the unit closes itself through these, which the
    // destructor runs while the wrapper still lives. Message thread.
    void addViewCloser(void* key, std::function<void()> closer);
    void removeViewCloser(void* key);

    // AUBase
    void PostConstructor() override;
    OSStatus Initialize() override;
    OSStatus Reset(AudioUnitScope scope, AudioUnitElement element) override;

    bool CanScheduleParameters() const noexcept override { return false; }
    bool StreamFormatWritable(AudioUnitScope scope,
                              AudioUnitElement element) override;
    UInt32 SupportedNumChannels(const AUChannelInfo** outInfo) override;
    bool ValidFormat(AudioUnitScope scope,
                     AudioUnitElement element,
                     const AudioStreamBasicDescription& format) override;

    OSStatus GetParameterList(AudioUnitScope scope,
                              AudioUnitParameterID* outList,
                              UInt32& outCount) override;
    OSStatus GetParameterInfo(AudioUnitScope scope,
                              AudioUnitParameterID id,
                              AudioUnitParameterInfo& info) override;
    OSStatus SetParameter(AudioUnitParameterID id,
                          AudioUnitScope scope,
                          AudioUnitElement element,
                          AudioUnitParameterValue value,
                          UInt32 bufferOffset) noexcept override;
    OSStatus GetParameterValueStrings(AudioUnitScope scope,
                                      AudioUnitParameterID id,
                                      CFArrayRef* outStrings) override;

    // kAudioUnitProperty_ParameterClumpName: the name of the group whose clump
    // GetParameterInfo gave (ParameterSpace::addClump), +1 for the host.
    OSStatus CopyClumpName(AudioUnitScope scope,
                           UInt32 clumpId,
                           UInt32 desiredNameLength,
                           CFStringRef* outClumpName) override;

    OSStatus GetPropertyInfo(AudioUnitPropertyID id,
                             AudioUnitScope scope,
                             AudioUnitElement element,
                             UInt32& outDataSize,
                             bool& outWritable) override;
    OSStatus GetProperty(AudioUnitPropertyID id,
                         AudioUnitScope scope,
                         AudioUnitElement element,
                         void* outData) override;
    OSStatus SetProperty(AudioUnitPropertyID id,
                         AudioUnitScope scope,
                         AudioUnitElement element,
                         const void* inData,
                         UInt32 inDataSize) override;

    // After the base's: a new kAudioUnitProperty_ContextName, which the base
    // stores without calling SetProperty, becomes the plugin's track name.
    void PropertyChanged(AudioUnitPropertyID id,
                         AudioUnitScope scope,
                         AudioUnitElement element) override;

    Float64 GetLatency() noexcept override;
    Float64 GetTailTime() noexcept override;
    bool SupportsTail() noexcept override { return true; }

    OSStatus SaveState(CFPropertyListRef* outData) override;
    OSStatus RestoreState(CFPropertyListRef plist) override;

    OSStatus Render(AudioUnitRenderActionFlags& flags,
                    const AudioTimeStamp& timestamp,
                    UInt32 frames) noexcept override;

    // AUMIDIBase, from any host thread: staged under a spin lock for the next
    // render, which hands every event to MIDI-in bus 0 at its frame, since AU
    // delivers one MIDI stream.
    OSStatus HandleNoteOn(UInt8 channel,
                          UInt8 note,
                          UInt8 velocity,
                          UInt32 frame) noexcept override;
    OSStatus HandleNoteOff(UInt8 channel,
                           UInt8 note,
                           UInt8 velocity,
                           UInt32 frame) noexcept override;
    OSStatus HandleControlChange(UInt8 channel,
                                 UInt8 controller,
                                 UInt8 value,
                                 UInt32 frame) noexcept override;
    OSStatus HandlePitchWheel(UInt8 channel,
                              UInt8 lsb,
                              UInt8 msb,
                              UInt32 frame) noexcept override;
    OSStatus HandleChannelPressure(UInt8 channel,
                                   UInt8 value,
                                   UInt32 frame) noexcept override;
    OSStatus HandlePolyPressure(UInt8 channel,
                                UInt8 note,
                                UInt8 value,
                                UInt32 frame) noexcept override;
    OSStatus HandleProgramChange(UInt8 channel, UInt8 program) noexcept override;
    OSStatus HandleSysEx(const UInt8* data, UInt32 length) noexcept override;

    // The base diverts CC 120, 121 and 123 and program changes to handlers with
    // no frame; this puts them back on the MIDI-in bus at their offset.
    OSStatus HandleNonNoteEvent(UInt8 status,
                                UInt8 channel,
                                UInt8 data1,
                                UInt8 data2,
                                UInt32 frame) noexcept override;

    // HostEditListener, message thread
    void beginParameterEdit(int index) noexcept override;
    void performParameterEdit(int index, float normalized) noexcept override;
    void endParameterEdit(int index) noexcept override;
    void latencyChanged() noexcept override;
    void parameterInfoChanged() noexcept override;
    void parameterValuesChanged() noexcept override;
    void stateChanged() noexcept override;

    // Message thread: a value event for each parameter the render thread found
    // the plugin had moved on its own. The value timer runs it at 30 Hz.
    void postMovedParameters() noexcept;

private:
    Adapter(AudioComponentInstance instance,
            const PluginDescription& description,
            OwningPointer<Plugin> plugin);

    struct ViewCloser
    {
        void* key = nullptr;
        std::function<void()> close;
    };

    // A main-queue timer calling postMovedParameters(), defined in Adapter.cpp
    // so no dispatch type reaches an Objective-C++ TU including this header.
    class ValueTimer;

    double outputSampleRate();
    BusLayout negotiatedLayout();
    bool isPublishedCount(bool input, int channels) const noexcept;
    int indexOfExposed(AudioUnitParameterID id) const noexcept;
    bool isBypassed() const noexcept;
    OSStatus setBypass(bool on) noexcept;

    void pushParametersToPlugin();
    void mirrorParametersToHost();
    void notifyHostIfLatencyChanged();
    void notifyHost(int index, AudioUnitEventType type) noexcept;

    // The plugin's value of `slot` (or of every slot) is now what the host has.
    void markToldHost(int slot) noexcept;
    void markAllToldHost() noexcept;

    // Into Globals() and a value event for each slot whose value the host does
    // not have yet (toldHost) or that the render found moved; returns whether
    // there was any.
    bool postUntoldValues() noexcept;

    void reconcileParameters() noexcept;
    void bindInputs(AudioUnitRenderActionFlags& flags,
                    const AudioTimeStamp& timestamp,
                    UInt32 frames) noexcept;
    void takeStagedMidi() noexcept;
    void bindOutputs(UInt32 frames) noexcept;
    void emitMidiOut(const AUMIDIOutputCallbackStruct* callback,
                     const AudioTimeStamp& timestamp) noexcept;
    void pushMidi(const MIDI::Event& event) noexcept;

    PluginWrapper pluginWrapper;

    // How the host sees the parameters, by the description's resolved policy.
    ParameterSpace space;

    // The plugin's value of an entry in the host's space (ParameterSpace::toHost),
    // and a value in that space written into the plugin.
    float hostValueOf(int index) const noexcept;
    void setFromHost(int index, float hostValue) noexcept;

    // Whether the host addresses the entry (ParameterSpace::isListed).
    bool isListed(int index) const noexcept;

    // Indices of the entries the host addresses, in declaration order, and what
    // each read in Globals() (in the host's space) and in the plugin (plain) at
    // the end of the last block.
    Vector<int> exposed;
    Vector<float> lastHostValue;
    Vector<float> lastPluginValue;

    // GetParameterList's answer (ParameterSpace::listOrder); unused when the
    // SDK's indexed list answers (ParameterSpace::usesSdkIndexedList).
    Vector<AudioUnitParameterID> listOrder;
    bool sdkIndexedList = false;

    // Per slot, raised by the render thread when it wrote a value the plugin
    // moved on its own into Globals(), taken by postMovedParameters().
    std::unique_ptr<std::atomic<bool>[]> movedByPlugin;

    // Per slot, the plugin's value (plain) the host last had: from its own write,
    // a restore it made, or an event we posted. A value event reads its value when
    // it is posted and lands on the host's run loop later, so one posted for a
    // value the host already has can arrive after the host wrote or restored
    // another, and overwrite what the host holds with the older one.
    std::unique_ptr<std::atomic<float>[]> toldHost;

    // The slot of each parameter index, -1 for one the host does not address.
    Vector<int> slotOfIndex;
    std::unique_ptr<ValueTimer> valueTimer;

    // Handed to the host by SupportedNumChannels, so it outlives the call.
    Vector<AUChannelInfo> channelInfos;

    Vector<Vector<const float*>> inputTables;
    Vector<Vector<float*>> outputTables;
    Vector<char> inputBound;

    // Reserved in PostConstructor; a full buffer drops the event.
    MIDI::Buffer stagedMidi;
    EA::Locks::PrimitiveSpinLock stagedMidiLock;

    RealtimeSwap<AUMIDIOutputCallbackStruct> midiOutputCallback;
    Vector<Byte> midiPacketBuffer;

    std::atomic<bool> resetPending {false};

    // kAudioUnitProperty_OfflineRender, which the SDK does not handle: each
    // block's ProcessContext::isOffline.
    std::atomic<bool> offlineRender {false};

    // What the host last fetched; -1 while it has no baseline to be told about.
    std::atomic<int> lastReportedLatency {-1};

    Vector<ViewCloser> viewClosers;
};

} // namespace MakeASound::AU
