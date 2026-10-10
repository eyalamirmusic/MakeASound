#include "SynthPlugin.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace MakeASoundExamples
{

static float noteToFrequency(int note) noexcept
{ return 440.f * std::pow(2.f, static_cast<float>(note - 69) / 12.f); }

void SynthPlugin::prepare(const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    // A pitch is held at most once, so 128 entries cover every stack.
    heldNotes.reserve(128);

    level.setSampleRate(sampleRate);
    level.setRampTime(0.02f);
    reset();
}

void SynthPlugin::reset() noexcept
{
    heldNotes.clear();
    currentNote = -1;
    phase = 0.f;
    velocity = 0.f;
    envelope = 0.f;
    level.reset(params.level.gain());
}

void SynthPlugin::process(ProcessContext& context) noexcept
{
    auto settings = BlockSettings {params.waveform.getIndex(),
                                   envelopeStep(params.attack.get()),
                                   envelopeStep(params.release.get())};

    level.setTarget(params.level.gain());

    auto& output = context.mainOutput();
    auto cursor = 0;

    for (const auto& event: context.mainMidiIn())
    {
        auto offset = std::clamp(event.sampleOffset, cursor, output.getNumSamples());
        auto segment = output.getSubBuffer(cursor, offset - cursor);
        render(segment, settings);
        applyEvent(event);
        cursor = offset;
    }

    auto tail = output.getSubBuffer(cursor);
    render(tail, settings);
}

void SynthPlugin::render(Buffer& output, const BlockSettings& settings) noexcept
{
    if (output.getNumChannels() == 0 || output.getNumSamples() == 0)
        return;

    auto first = output.getChannel(0);
    auto gate = currentNote >= 0;

    for (auto& sample: first)
    {
        if (gate)
            envelope = std::min(1.f, envelope + settings.attackStep);
        else
            envelope = std::max(0.f, envelope - settings.releaseStep);

        auto gain = level.next() * velocity * envelope;
        sample = envelope > 0.f ? oscillator(settings.waveform) * gain : 0.f;
    }

    for (auto channel = 1; channel < output.getNumChannels(); ++channel)
    {
        auto out = output.getChannel(channel);
        std::copy(first.begin(), first.end(), out.begin());
    }
}

float SynthPlugin::oscillator(int waveform) noexcept
{
    auto value = 0.f;

    if (waveform == 1)
        value = 2.f * phase - 1.f;
    else if (waveform == 2)
        value = phase < 0.5f ? 1.f : -1.f;
    else
        value = std::sin(2.f * std::numbers::pi_v<float> * phase);

    phase += increment;
    phase -= std::floor(phase);

    return value;
}

void SynthPlugin::applyEvent(const MIDI::Event& event) noexcept
{
    event.visit(MIDI::overloaded {
        [&](const MIDI::NoteOn& on) { noteOn(on.pitch, on.velocity); },
        [&](const MIDI::NoteOff& off) { noteOff(off.pitch); },
        [&](const MIDI::ControlChange& cc)
        {
            if (cc.controller == 123)
                allNotesOff();
        },
        [&](const auto&) {},
    });
}

void SynthPlugin::noteOn(int note, float velocityToUse) noexcept
{
    auto wasSounding = currentNote >= 0;

    std::erase(heldNotes, note);
    heldNotes.push_back(note);

    currentNote = note;
    velocity = velocityToUse;
    increment = noteToFrequency(note) / static_cast<float>(sampleRate);

    if (!wasSounding || !params.legato.isOn())
    {
        envelope = 0.f;
        phase = 0.f;
    }
}

void SynthPlugin::noteOff(int note) noexcept
{
    std::erase(heldNotes, note);

    if (heldNotes.empty())
    {
        currentNote = -1;
        return;
    }

    if (heldNotes.back() != currentNote)
    {
        currentNote = heldNotes.back();
        increment = noteToFrequency(currentNote) / static_cast<float>(sampleRate);
    }
}

void SynthPlugin::allNotesOff() noexcept
{
    heldNotes.clear();
    currentNote = -1;
}

float SynthPlugin::envelopeStep(float seconds) const noexcept
{ return 1.f / std::max(1.f, seconds * static_cast<float>(sampleRate)); }

} // namespace MakeASoundExamples

namespace MakeASound
{

ModuleDescription describeModule()
{
    auto module = ModuleDescription {};
    module.vendor = "MakeASound";
    module.manufacturerCode = "MkAS";
    module.plugins.add(
        {.name = "Synth",
         .category = Category::Instrument,
         .pluginCode = "Synt",
         .create = [] { return EA::makeOwned<MakeASoundExamples::SynthPlugin>(); }});
    return module;
}

} // namespace MakeASound
