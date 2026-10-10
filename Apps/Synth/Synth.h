#pragma once

#include <MakeASound/MakeASound.h>
#include <eacp/Core/Core.h>

#include <functional>
#include <numbers>

namespace MS = MakeASound;
namespace MIDI = MS::MIDI;

struct AudioControls
{
    MIRO_REFLECT(playing, gain, note, frequency, velocity)

    bool playing {};
    double gain {};
    int note {-1};
    double frequency {};
    double velocity {};
};

struct Synth : MS::Processor
{
    static constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;

    Synth() { heldNotes.reserve(120); }

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

    using MidiAppliedCallback = std::function<void(const MIDI::Event&)>;

    MS::BusLayout getBusLayout() const override
    { return MS::BusLayout::instrument(); }

    void prepare(const MS::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        reset();
    }

    void process(MS::ProcessContext& ctx) noexcept override
    {
        auto& output = ctx.mainOutput();
        auto cursor = 0;

        for (auto& event: ctx.mainMidiIn())
        {
            auto block = output.getSubBuffer(cursor, event.sampleOffset - cursor);
            render(block);
            applyMidiOnAudioThread(event);
            cursor = event.sampleOffset;
        }

        auto tail = output.getSubBuffer(cursor);
        render(tail);
    }

    void reset() noexcept override { voice.phase = 0.0f; }

    void render(MS::Buffer& output) noexcept
    {
        if (output.getNumSamples() <= 0 || output.getNumChannels() <= 0)
            return;

        auto noteValue = note.load();
        auto velocityValue = velocity.load();
        auto gainValue = gain.load();

        auto first = output.getChannel(0);

        if (noteValue < 0)
        {
            std::fill(first.begin(), first.end(), 0.0f);
        }
        else
        {
            auto frequency = midiNoteToFrequency(noteValue);
            auto increment = twoPi * frequency / static_cast<float>(sampleRate);
            auto amplitude = gainValue * velocityValue;

            for (auto& sample: first)
                sample = voice.renderSample(increment) * amplitude;
        }

        for (auto channel = 1; channel < output.getNumChannels(); ++channel)
        {
            auto out = output.getChannel(channel);
            std::copy(first.begin(), first.end(), out.begin());
        }
    }

    // The UI's MIDI log; the callAsync is the demo's marshalling, not the
    // library's, and allocates on the audio thread.
    void applyMidiOnAudioThread(const MIDI::Event& midiEvent)
    {
        applyMidiEvent(midiEvent);

        if (midiAppliedCb)
            eacp::Threads::callAsync([midiEvent, cb = midiAppliedCb]
                                     { cb(midiEvent); });
    }

    // Called on the audio thread.
    void applyMidiEvent(const MIDI::Event& event)
    {
        event.visit(MIDI::overloaded {
            [&](const MIDI::NoteOn& n) { noteOn(n.pitch, n.velocity); },
            [&](const MIDI::NoteOff& n) { noteOff(n.pitch); },
            [&](const MIDI::ControlChange& cc)
            {
                if (cc.controller == 123) // all notes off
                    releaseAllNotes();
                else if (cc.controller == 7) // channel volume
                    gain.store(cc.value);
            },
            [&](const auto&) {},
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

    MidiAppliedCallback midiAppliedCb;

    SineVoice voice;
    std::vector<int> heldNotes;
    int sampleRate {44100};
};
