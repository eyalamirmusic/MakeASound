#pragma once

#include <MakeASound/MakeASound.h>
#include <numbers>

struct AudioControls
{
    MIRO_REFLECT(playing, gain, note, frequency, velocity)

    bool playing {};
    double gain {};
    int note {-1};
    double frequency {};
    double velocity {};
};

struct Synth
{
    static constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;

    static float midiNoteToFrequency(int noteToConvert)
    {
        return 440.0f
               * std::pow(2.0f, static_cast<float>(noteToConvert - 69) / 12.0f);
    }

    struct SineVoice
    {
        float renderSample(float increment)
        {
            auto value = std::sin(phase);
            phase += increment;

            if (phase >= twoPi)
                phase -= twoPi;

            return value;
        }

        float phase {0.0f};
    };

    void reset() { voice.phase = 0.0f; }

    void render(MakeASound::AudioCallbackInfo& info, int startSample, int endSample)
    {
        if (startSample >= endSample || info.numOutputs <= 0)
            return;

        auto noteValue = note.load();
        auto velocityValue = velocity.load();
        auto gainValue = gain.load();

        auto first = info.getOutput(0);

        if (noteValue < 0)
        {
            std::fill(first.begin() + startSample,
                      first.begin() + endSample,
                      0.0f);
        }
        else
        {
            auto frequency = midiNoteToFrequency(noteValue);
            auto increment = twoPi * frequency / static_cast<float>(info.sampleRate);
            auto amplitude = gainValue * velocityValue;

            for (auto i = static_cast<std::size_t>(startSample);
                 i < static_cast<std::size_t>(endSample);
                 ++i)
                first[i] = voice.renderSample(increment) * amplitude;
        }

        for (auto channel = 1; channel < info.numOutputs; ++channel)
        {
            auto out = info.getOutput(channel);
            std::copy(first.begin() + startSample,
                      first.begin() + endSample,
                      out.begin() + startSample);
        }
    }

    // Audio-thread: apply a typed MIDI event and update synth state.
    void applyMidiEvent(const MakeASound::MIDI::Event& event)
    {
        event.visit(MakeASound::MIDI::overloaded {
            [&](const MakeASound::MIDI::NoteOn& n) { noteOn(n.pitch, n.velocity); },
            [&](const MakeASound::MIDI::NoteOff& n) { noteOff(n.pitch); },
            [&](const MakeASound::MIDI::ControlChange& cc)
            {
                if (cc.controller == 123) // all notes off
                    releaseAllNotes();
                else if (cc.controller == 7) // channel volume
                    gain.store(cc.value);
            },
            [&](const auto&) {}, // ignore everything else for this demo
        });
    }

    void noteOn(int noteToPlay, float velocityToUse)
    {
        std::erase(heldNotes, noteToPlay);
        heldNotes.push_back(noteToPlay);
        note.store(noteToPlay);
        velocity.store(velocityToUse);
    }

    void noteOff(int noteToStop)
    {
        std::erase(heldNotes, noteToStop);

        if (heldNotes.empty())
        {
            note.store(-1);
            velocity.store(0.0f);
        }
        else
        {
            note.store(heldNotes.back());
        }
    }

    void releaseAllNotes()
    {
        heldNotes.clear();
        note.store(-1);
        velocity.store(0.0f);
    }

    void setGain(float gainToUse) { gain.store(gainToUse); }

    AudioControls makeControls() const
    {
        auto noteValue = note.load();
        auto velocityValue = velocity.load();

        auto controls = AudioControls {};
        controls.playing = noteValue >= 0;
        controls.gain = static_cast<double>(gain.load());
        controls.note = noteValue;
        controls.velocity = static_cast<double>(velocityValue);
        controls.frequency =
            noteValue >= 0 ? static_cast<double>(midiNoteToFrequency(noteValue))
                           : 0.0;
        return controls;
    }

    std::atomic<int> note {-1};
    std::atomic<float> velocity {0.0f};
    std::atomic<float> gain {0.5f};

    SineVoice voice;
    std::vector<int> heldNotes;
};
