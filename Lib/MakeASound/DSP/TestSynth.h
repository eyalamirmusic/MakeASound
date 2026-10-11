#pragma once

#include "../Audio/Processor.h"
#include "../Realtime/Smoother.h"

#include <vector>

namespace MakeASound::DSP
{

enum class Waveform
{
    Sine,
    Saw,
    Square
};

// A monophonic test instrument with last-note priority: releasing a note falls
// back to the newest one still held. One oscillator, a linear attack/release
// envelope and a smoothed output level. The whole block is process(); a host
// that wants to see each event drives render() and handleEvent() itself.
class TestSynth : public Processor
{
public:
    struct Settings
    {
        Waveform waveform = Waveform::Sine;
        float attackSeconds = 0.005f;
        float releaseSeconds = 0.2f;
        float gain = 0.25f;
        bool legato = false;
    };

    BusLayout getBusLayout() const override { return BusLayout::instrument(); }

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    // Audio thread, before the block they apply to. The gain is ramped.
    void setSettings(const Settings& settingsToUse) noexcept;
    const Settings& getSettings() const noexcept { return settings; }

    void render(Buffer& output) noexcept;
    void handleEvent(const MIDI::Event& event) noexcept;
    void allNotesOff() noexcept;

    int getCurrentNote() const noexcept { return currentNote; }
    float getVelocity() const noexcept { return velocity; }
    bool isPlaying() const noexcept { return currentNote >= 0; }

    static float noteToFrequency(int note) noexcept;

private:
    void noteOn(int note, float velocityToUse) noexcept;
    void noteOff(int note) noexcept;
    void setNote(int note) noexcept;
    float oscillator() noexcept;
    float envelopeStep(float seconds) const noexcept;

    Settings settings;
    std::vector<int> heldNotes;
    int sampleRate = 44100;

    int currentNote = -1;
    float phase = 0.f;
    float increment = 0.f;
    float velocity = 0.f;
    float envelope = 0.f;
    Smoother level;
};

} // namespace MakeASound::DSP
