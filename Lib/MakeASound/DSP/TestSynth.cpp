#include "TestSynth.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace MakeASound::DSP
{

float TestSynth::noteToFrequency(int note) noexcept
{
    return 440.f * std::pow(2.f, static_cast<float>(note - 69) / 12.f);
}

void TestSynth::prepare(const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    // A pitch is held at most once, so 128 entries cover every stack.
    heldNotes.reserve(128);

    level.setSampleRate(sampleRate);
    level.setRampTime(0.02f);
    reset();
}

void TestSynth::reset() noexcept
{
    heldNotes.clear();
    currentNote = -1;
    phase = 0.f;
    velocity = 0.f;
    envelope = 0.f;
    level.reset(settings.gain);
}

void TestSynth::setSettings(const Settings& settingsToUse) noexcept
{
    settings = settingsToUse;
    level.setTarget(settings.gain);
}

void TestSynth::process(ProcessContext& context) noexcept
{
    auto& output = context.mainOutput();
    auto cursor = 0;

    for (const auto& event: context.mainMidiIn())
    {
        auto offset = std::clamp(event.sampleOffset, cursor, output.getNumSamples());
        auto segment = output.getSubBuffer(cursor, offset - cursor);
        render(segment);
        handleEvent(event);
        cursor = offset;
    }

    auto tail = output.getSubBuffer(cursor);
    render(tail);
}

void TestSynth::render(Buffer& output) noexcept
{
    if (output.getNumChannels() == 0 || output.getNumSamples() == 0)
        return;

    auto attackStep = envelopeStep(settings.attackSeconds);
    auto releaseStep = envelopeStep(settings.releaseSeconds);
    auto first = output.getChannel(0);

    for (auto& sample: first)
    {
        if (isPlaying())
            envelope = std::min(1.f, envelope + attackStep);
        else
            envelope = std::max(0.f, envelope - releaseStep);

        auto gain = level.next() * velocity * envelope;
        sample = envelope > 0.f ? oscillator() * gain : 0.f;
    }

    for (auto channel = 1; channel < output.getNumChannels(); ++channel)
    {
        auto out = output.getChannel(channel);
        std::copy(first.begin(), first.end(), out.begin());
    }
}

float TestSynth::oscillator() noexcept
{
    auto value = 0.f;

    switch (settings.waveform)
    {
        case Waveform::Saw:
            value = 2.f * phase - 1.f;
            break;
        case Waveform::Square:
            value = phase < 0.5f ? 1.f : -1.f;
            break;
        case Waveform::Sine:
            value = std::sin(2.f * std::numbers::pi_v<float> * phase);
            break;
    }

    phase += increment;
    phase -= std::floor(phase);

    return value;
}

void TestSynth::handleEvent(const MIDI::Event& event) noexcept
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

void TestSynth::noteOn(int note, float velocityToUse) noexcept
{
    auto wasSounding = isPlaying();

    std::erase(heldNotes, note);
    heldNotes.push_back(note);

    setNote(note);
    velocity = velocityToUse;

    if (!wasSounding || !settings.legato)
    {
        envelope = 0.f;
        phase = 0.f;
    }
}

void TestSynth::noteOff(int note) noexcept
{
    std::erase(heldNotes, note);

    if (heldNotes.empty())
        currentNote = -1;
    else if (heldNotes.back() != currentNote)
        setNote(heldNotes.back());
}

void TestSynth::allNotesOff() noexcept
{
    heldNotes.clear();
    currentNote = -1;
}

void TestSynth::setNote(int note) noexcept
{
    currentNote = note;
    increment = noteToFrequency(note) / static_cast<float>(sampleRate);
}

float TestSynth::envelopeStep(float seconds) const noexcept
{
    return 1.f / std::max(1.f, seconds * static_cast<float>(sampleRate));
}

} // namespace MakeASound::DSP
