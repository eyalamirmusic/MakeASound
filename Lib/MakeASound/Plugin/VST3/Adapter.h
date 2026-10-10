#pragma once

#include "VST3Common.h"
#include "../Host/PluginWrapper.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <atomic>

namespace MakeASound::VST3
{

// The one VST3 class every plugin ships as: component and controller in one
// object over a PluginWrapper, turning host calls into the wrapper's per-block
// steps. MIDI controllers arrive through IMidiMapping on hidden shadow
// parameters; the plugin's own edits leave through HostEditListener.
class Adapter
    : public Vst::SingleComponentEffect
    , public Vst::IMidiMapping
    , public HostEditListener
{
public:
    // Installs itself as the plugin's HostEditListener; the destructor removes it.
    explicit Adapter(OwningPointer<Plugin> plugin);
    ~Adapter() override;

    Plugin& plugin() noexcept;
    PluginWrapper& wrapper() noexcept;

    // IPluginBase
    tresult PLUGIN_API initialize(FUnknown* context) override;
    tresult PLUGIN_API terminate() override;

    // IComponent
    tresult PLUGIN_API setActive(TBool state) override;
    tresult PLUGIN_API setState(IBStream* state) override;
    tresult PLUGIN_API getState(IBStream* state) override;

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(Vst::SpeakerArrangement* inputs,
                                          int32 numIns,
                                          Vst::SpeakerArrangement* outputs,
                                          int32 numOuts) override;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) override;
    tresult PLUGIN_API setProcessing(TBool state) override;
    uint32 PLUGIN_API getLatencySamples() override;
    uint32 PLUGIN_API getTailSamples() override;
    tresult PLUGIN_API process(Vst::ProcessData& data) noexcept override;

    // IEditController
    tresult PLUGIN_API setComponentState(IBStream* state) override;
    Vst::ParamValue PLUGIN_API getParamNormalized(Vst::ParamID id) override;
    tresult PLUGIN_API setParamNormalized(Vst::ParamID id,
                                          Vst::ParamValue value) override;
    Steinberg::IPlugView* PLUGIN_API createView(FIDString name) override;

    // IMidiMapping
    tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex,
                                                   Steinberg::int16 channel,
                                                   Vst::CtrlNumber controller,
                                                   Vst::ParamID& id) override;

    // HostEditListener, message thread
    void beginParameterEdit(int index) noexcept override;
    void performParameterEdit(int index, float normalized) noexcept override;
    void endParameterEdit(int index) noexcept override;
    void beginParameterEditGroup() noexcept override;
    void endParameterEditGroup() noexcept override;
    void latencyChanged() noexcept override;
    void parameterInfoChanged() noexcept override;

    tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                      void** obj) override;
    REFCOUNT_METHODS(Vst::SingleComponentEffect)

private:
    void addBuses();
    void registerParameters();
    void registerShadowParameters();
    void notifyHostIfLatencyChanged();
    void applyParameterChanges(Vst::IParameterChanges& changes) noexcept;
    void collectEvents(Vst::IEventList& events) noexcept;
    void bindBuses(Vst::ProcessData& data) noexcept;
    void emitEvents(Vst::IEventList& events) noexcept;

    PluginWrapper pluginWrapper;
    int numMidiInputs = 0;
    bool active = false;

    // A process() before the first activation renders silence.
    bool prepared = false;

    // What the host last fetched; -1 while it has no baseline to restart from.
    std::atomic<int> lastReportedLatency {-1};
};

} // namespace MakeASound::VST3
