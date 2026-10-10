#pragma once

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <vector>

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

// Monophonic, last-note priority: releasing a note falls back to the newest
// one still held.
struct SynthPlugin : StatePlugin<State<SynthParams>>
{
    std::string_view name() const override { return "Synth"; }

    BusLayout getBusLayout() const override { return BusLayout::instrument(); }

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

private:
    struct BlockSettings
    {
        int waveform = 0;
        float attackStep = 1.f;
        float releaseStep = 1.f;
    };

    void render(Buffer& output, const BlockSettings& settings) noexcept;
    float oscillator(int waveform) noexcept;

    void applyEvent(const MIDI::Event& event) noexcept;
    void noteOn(int note, float velocityToUse) noexcept;
    void noteOff(int note) noexcept;
    void allNotesOff() noexcept;

    float envelopeStep(float seconds) const noexcept;

    std::vector<int> heldNotes;
    int sampleRate = 44100;

    int currentNote = -1;
    float phase = 0.f;
    float increment = 0.f;
    float velocity = 0.f;
    float envelope = 0.f;
    Smoother level;
};

} // namespace MakeASoundExamples
