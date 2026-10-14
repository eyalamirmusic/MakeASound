#pragma once

// VST3-only, after VST3Common.h.

#include "../../Plugin/Parameters/Parameter.h"

#include <pluginterfaces/vst/ivsteditcontroller.h>

#include <atomic>
#include <string>

namespace MakeASound::Hosting
{

// Where a host-side edit of a hosted VST3 parameter goes: the controller, so the
// plugin's own GUI follows, and the processor's next block, which is the only way
// a VST3 processor hears it. VST3HostedPlugin implements it.
struct VST3ParameterSink
{
    virtual ~VST3ParameterSink() = default;

    // Message thread; the value is already in the parameter.
    virtual void parameterEdited(Steinberg::Vst::ParamID id, float normalized) = 0;
};

// One parameter of a hosted VST3 plugin, in the plugin's normalized space: its
// plain range is 0..1, stepped where the plugin says, and its text comes from
// the plugin's controller. Its id is the ParamID in decimal and its host id the
// ParamID itself, all 32 bits. The value is a relaxed atomic both threads read.
class VST3HostedParameter : public Parameter
{
public:
    VST3HostedParameter(Steinberg::Vst::IEditController& controllerToUse,
                        const Steinberg::Vst::ParameterInfo& info,
                        VST3ParameterSink& sinkToUse);

    Steinberg::Vst::ParamID paramId() const noexcept { return vstId; }

    float minValue() const noexcept override { return 0.f; }
    float maxValue() const noexcept override { return 1.f; }
    float defaultValue() const noexcept override { return initial; }
    int numSteps() const noexcept override { return steps; }
    bool isDiscrete() const noexcept override { return steps > 0; }
    bool isBoolean() const noexcept override { return steps == 1; }

    // A host-side edit, off the audio thread: kept, then sent to the controller
    // and queued for the processor.
    void setValue(float normalized) noexcept override;
    float getValue() const noexcept override;

    // The plugin's own text for a value, and its parse of one (the controller's
    // getParamStringByValue / getParamValueByString). Message thread.
    std::string valueToText(float normalized) const override;
    float textToValue(std::string_view text) const override;

    // A value the plugin reported (its GUI, its processor, a state load): kept,
    // nothing sent. Any thread.
    void store(float normalized) noexcept;

    // Re-reads the title, short title and units after kParamTitlesChanged. True
    // when one changed. The steps, default and flags stay as first read: the
    // audio thread reads those unsynchronized, and a plugin that changes its
    // value model must be reloaded. Message thread.
    bool updateInfo(const Steinberg::Vst::ParameterInfo& info);

    // Marked on the audio thread when the processor reported a value, consumed
    // on the message thread to tell the controller.
    void markChangedByProcessor() noexcept;
    bool takeChangedByProcessor() noexcept;

private:
    Steinberg::Vst::IEditController& controller;
    VST3ParameterSink& sink;
    Steinberg::Vst::ParamID vstId;
    float initial;
    int steps;
    std::atomic<float> value;
    std::atomic<bool> changedByProcessor {false};
};

} // namespace MakeASound::Hosting
