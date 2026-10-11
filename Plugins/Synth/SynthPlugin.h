#pragma once

#include <MakeASound/Plugin/MakeASoundPlugin.h>
#include <MakeASound/DSP/MakeASoundDSP.h>

namespace MakeASoundExamples
{
using namespace MakeASound;

struct SynthParams : ParameterGroup
{
    SynthParams() { add(waveform, attack, release, level, legato); }

    ChoiceParam waveform {"Waveform", {"Sine", "Saw", "Square"}, 0};
    TimeParam attack {"Attack", 0.001f, 2.f, 0.005f};
    TimeParam release {"Release", 0.001f, 2.f, 0.2f};
    DecibelParam level {"Level", -60.f, 0.f, -12.f};
    BoolParam legato {"Legato", false};
};

// The TestSynth with its settings on parameters.
struct SynthPlugin : StatePlugin<State<SynthParams>>
{
    std::string_view name() const override { return "Synth"; }

    BusLayout getBusLayout() const override { return synth.getBusLayout(); }

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

private:
    DSP::TestSynth::Settings settingsFromParams() const noexcept;

    DSP::TestSynth synth;
};

} // namespace MakeASoundExamples
