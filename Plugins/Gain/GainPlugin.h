#pragma once

#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace MakeASoundExamples
{
using namespace MakeASound;

struct GainParams : ParameterGroup
{
    GainParams() { add(gain, bypass); }

    // versionHint 1: the first release's parameters, ahead of any added later in
    // an AU's parameter list.
    DecibelParam gain {"Gain", -60.f, 12.f, 0.f, {.versionHint = 1}};
    BoolParam bypass {"Bypass", false, {.bypass = true, .versionHint = 1}};
};

struct GainPlugin : StatePlugin<State<GainParams>>
{
    std::string_view name() const override { return "Gain"; }

    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    float targetGain() const noexcept;

    Smoother gain;
};

} // namespace MakeASoundExamples
