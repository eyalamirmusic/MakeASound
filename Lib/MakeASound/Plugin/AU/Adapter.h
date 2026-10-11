#pragma once

#include "AUCommon.h"
#include "../Core/Description.h"
#include "../Host/PluginWrapper.h"
#include "../Realtime/RealtimeSwap.h"
#include "../../Realtime/SpinLock.h"

#include <AudioToolbox/AudioUnitUtilities.h>

#include <atomic>
#include <functional>

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

private:
    Adapter(AudioComponentInstance instance, OwningPointer<Plugin> plugin);

    struct ViewCloser
    {
        void* key = nullptr;
        std::function<void()> close;
    };

    double outputSampleRate();
    BusLayout negotiatedLayout();
    bool isPublishedCount(bool input, int channels) const noexcept;
    int indexOfExposed(AudioUnitParameterID id) const noexcept;

    void pushParametersToPlugin();
    void mirrorParametersToHost();
    void notifyHostIfLatencyChanged();
    void notifyHost(int index, AudioUnitEventType type) noexcept;

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

    // Indices of the host-exposed entries, in declaration order, and what each
    // read in Globals() and in the plugin at the end of the last block.
    Vector<int> exposed;
    Vector<float> lastHostValue;
    Vector<float> lastPluginValue;

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

    // What the host last fetched; -1 while it has no baseline to be told about.
    std::atomic<int> lastReportedLatency {-1};

    Vector<ViewCloser> viewClosers;
};

} // namespace MakeASound::AU
