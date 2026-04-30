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

    // Audio-thread: interpret a MIDI message and update synth state.
    void applyMidiMessage(const MakeASound::MidiMessage& msg)
    {
        if (msg.bytes.size() < 3)
            return;

        auto status = msg.bytes[0] & 0xF0;
        auto data1 = static_cast<int>(msg.bytes[1]);
        auto data2 = static_cast<int>(msg.bytes[2]);

        if (status == 0x90 && data2 > 0)
            noteOn(data1, static_cast<float>(data2) / 127.0f);
        else if (status == 0x80 || (status == 0x90 && data2 == 0))
            noteOff(data1);
        else if (status == 0xB0 && data1 == 123)
            releaseAllNotes();
        else if (status == 0xB0 && data1 == 7)
            gain.store(static_cast<float>(data2) / 127.0f);
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
