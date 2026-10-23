#pragma once

// VST3-only: the hosted plugin's SDK side, for code that has to reach the plugin
// itself (a hosted editor, a host nested in another host, a test standing in for
// the plugin's GUI). Include after VST3Common.h; everything else goes through
// HostedPlugin.

#include "../HostedPlugin.h"
#include "../../Realtime/SPSCQueue.h"
#include "VST3HostedParameter.h"

#include <public.sdk/source/vst/hosting/eventlist.h>
#include <public.sdk/source/vst/hosting/module.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivsthostapplication.h>
#include <pluginterfaces/vst/ivstmidicontrollers.h>
#include <pluginterfaces/vst/ivstprocesscontext.h>

#include <atomic>
#include <memory>
#include <vector>

namespace MakeASound::Hosting
{

// A loaded VST3 plugin: single-component or dual (its controller a class of its
// own, the two connected and the controller given the component's state before
// anything else asks it). The module stays loaded while the plugin lives.
class VST3HostedPlugin
    : public HostedPlugin
    , private VST3ParameterSink
{
public:
    VST3HostedPlugin(
        PluginDescription description,
        std::shared_ptr<::VST3::Hosting::Module> moduleToUse,
        Steinberg::IPtr<Steinberg::Vst::IHostApplication> hostToUse,
        Steinberg::IPtr<Steinberg::Vst::IComponent> componentToUse,
        Steinberg::IPtr<Steinberg::Vst::IEditController> controllerToUse,
        bool separateControllerToUse);

    ~VST3HostedPlugin() override;

    BusLayout getBusLayout() const override { return layout; }
    bool acceptsLayout(const BusLayout& proposed) const override;

    void prepare(const ProcessSpec& spec) override;
    void process(ProcessContext& context) noexcept override;

    int latencySamples() const noexcept override;
    int tailSamples() const noexcept override;

    // The component's own chunk, opaque; the context is not the plugin's to see.
    std::string saveState(StateContext context) override;
    void loadState(std::string_view data, StateContext context) override;

    // The controller's own getState/setState, which saveState and loadState
    // leave out. A single-component plugin answers them too, through the
    // same object; most write nothing.
    std::string saveControllerState() override;
    void loadControllerState(const std::string& data) override;

    bool setParameterFromAudioThread(int index, float normalized) noexcept override;
    void syncEditorWithProcessor() override;

    // The plugin's controller, which an editor asks for its view. Null when the
    // plugin has none.
    Steinberg::Vst::IEditController* editController() const noexcept
    {
        return controller.get();
    }

    // What this host installed on the controller: what the plugin's own GUI
    // calls when the user turns a knob. A host nested in another passes it on,
    // and a test stands in for that GUI through it.
    Steinberg::Vst::IComponentHandler* componentHandlerInterface() const noexcept;

    bool hasSeparateController() const noexcept { return separateController; }

    // The parameter carrying `id`, or -1.
    int indexOfParamId(Steinberg::Vst::ParamID id) const noexcept;

    // A JUCE host's parameter index as a parameters() index: JUCE counted every
    // controller parameter, hidden ones included, in getParameterInfo order
    // regrouped by unit (Compat::juceParameterTreeOrder), which is plain
    // getParameterInfo order unless a unit's parameters are interleaved with
    // others. -1 for a hidden one, one whose info the plugin refused, or past
    // the end.
    int indexForControllerIndex(int32_t controllerIndex) const noexcept override;

private:
    struct ComponentHandler;
    friend struct ComponentHandler;

    struct HostedGroup : ParameterGroup
    {
        void addAll(Vector<OwningPointer<VST3HostedParameter>>& params)
        {
            add(params);
        }
    };

    // A (ParamID, normalized) pair on its way to the processor's next block.
    struct ParamEdit
    {
        Steinberg::Vst::ParamID id = 0;
        float normalized = 0.f;
    };

    void readBusLayout();
    void buildParameters();
    bool applyArrangement(const BusLayout& proposed) const;
    void activateBuses(bool active);
    void stopProcessing();

    void parameterEdited(Steinberg::Vst::ParamID id, float normalized) override;

    void beginEdit(Steinberg::Vst::ParamID id);
    void performEdit(Steinberg::Vst::ParamID id, double normalized);
    void endEdit(Steinberg::Vst::ParamID id);
    void restart(Steinberg::int32 flags);

    void refreshValues();
    bool refreshInfo();

    VST3HostedParameter* findParameter(Steinberg::Vst::ParamID id) const noexcept;

    void bindAudio(ProcessContext& context, int numSamples) noexcept;
    void drainEdits() noexcept;
    void pushMidi(ProcessContext& context) noexcept;
    void readOutputParameters() noexcept;
    void readOutputEvents(ProcessContext& context) noexcept;
    void fillProcessContext(const Playhead& playhead) noexcept;

    std::shared_ptr<::VST3::Hosting::Module> module;
    Steinberg::IPtr<Steinberg::Vst::IHostApplication> hostApplication;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::IPtr<Steinberg::Vst::IMidiMapping> midiMapping;
    Steinberg::IPtr<ComponentHandler> handler;
    bool separateController = false;

    BusLayout layout;

    Vector<OwningPointer<VST3HostedParameter>> storage;
    HostedGroup group;

    // By ParamID, so the audio thread resolves an id with a binary search.
    Vector<VST3HostedParameter*> byId;

    // Per controller parameter, its storage index or -1.
    Vector<int> controllerIndices;

    Steinberg::Vst::ParameterChanges inputChanges;
    Steinberg::Vst::ParameterChanges outputChanges;
    Steinberg::Vst::EventList inputEvents;
    Steinberg::Vst::EventList outputEvents;

    // Sized in prepare() and only re-pointed per block. A channel the caller did
    // not supply reads `silence` or writes `scratch`: a VST3 plugin may touch
    // every channel of every bus it declares.
    Vector<Steinberg::Vst::AudioBusBuffers> inputBuses;
    Vector<Steinberg::Vst::AudioBusBuffers> outputBuses;
    Vector<std::vector<float*>> inputTables;
    Vector<std::vector<float*>> outputTables;
    std::vector<float> silence;
    std::vector<float> scratch;

    Steinberg::Vst::ProcessContext vstContext {};

    // Message thread to audio thread, drained at the top of each block.
    SPSCQueue<ParamEdit, 1024> pendingEdits;

    double sampleRate = 0.0;
    int maxBlockSize = 0;

    std::atomic<int> latency {0};
    std::atomic<bool> outputDirty {false};
    std::atomic<bool> prepared {false};

    bool active = false;
    bool processing = false;
};

} // namespace MakeASound::Hosting
