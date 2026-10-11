#pragma once

#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace MakeASoundExamples
{
using namespace MakeASound;

struct GainParams : ParameterGroup
{
    GainParams() { add(gain, bypass); }

    DecibelParam gain {"Gain", -60.f, 12.f, 0.f};
    BoolParam bypass {"Bypass", false, {.bypass = true}};
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
