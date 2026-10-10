#pragma once

#include <MakeASound/DSP/MakeASoundDSP.h>
#include <eacp/Core/Core.h>

#include <algorithm>
#include <atomic>
#include <functional>

namespace MS = MakeASound;
namespace MIDI = MS::MIDI;

struct AudioControls
{
    bool playing {};
    double gain {};
    int note {-1};
    double frequency {};
    double velocity {};

    MIRO_REFLECT(playing, gain, note, frequency, velocity)
};

// The TestSynth with the UI's gain, its MIDI log and what it shows as playing.
struct Synth : MS::Processor
{
    using MidiAppliedCallback = std::function<void(const MIDI::Event&)>;

    MS::BusLayout getBusLayout() const override { return synth.getBusLayout(); }

    void prepare(const MS::ProcessSpec& spec) override { synth.prepare(spec); }
    void reset() noexcept override { synth.reset(); }

    void process(MS::ProcessContext& ctx) noexcept override
    {
        if (releaseRequested.exchange(false))
            synth.allNotesOff();

        auto settings = synth.getSettings();
        settings.gain = gain.load();
        synth.setSettings(settings);

        auto& output = ctx.mainOutput();
        auto cursor = 0;

        for (auto& event: ctx.mainMidiIn())
        {
            auto offset =
                std::clamp(event.sampleOffset, cursor, output.getNumSamples());
            auto block = output.getSubBuffer(cursor, offset - cursor);
            synth.render(block);
            applyMidiOnAudioThread(event);
            cursor = offset;
        }

        auto tail = output.getSubBuffer(cursor);
        synth.render(tail);

        note.store(synth.getCurrentNote());
        velocity.store(synth.getVelocity());
    }

    // The UI's MIDI log; the callAsync is the demo's marshalling, not the
    // library's, and allocates on the audio thread.
    void applyMidiOnAudioThread(const MIDI::Event& midiEvent)
    {
        synth.handleEvent(midiEvent);

        if (auto* cc = midiEvent.asControlChange();
            cc != nullptr && cc->controller == 7)
            gain.store(cc->value);

        if (midiAppliedCb)
            eacp::Threads::callAsync([midiEvent, cb = midiAppliedCb]
                                     { cb(midiEvent); });
    }

    // Any thread: applied at the start of the next block.
    void releaseAllNotes() { releaseRequested.store(true); }

    void setGain(float gainToUse) { gain.store(gainToUse); }

    AudioControls makeControls() const
    {
        auto noteValue = note.load();

        auto controls = AudioControls {};
        controls.playing = noteValue >= 0;
        controls.gain = static_cast<double>(gain.load());
        controls.note = noteValue;
        controls.velocity = static_cast<double>(velocity.load());
        controls.frequency =
            noteValue >= 0
                ? static_cast<double>(MS::DSP::TestSynth::noteToFrequency(noteValue))
                : 0.0;
        return controls;
    }

    MidiAppliedCallback midiAppliedCb;

private:
    MS::DSP::TestSynth synth;

    std::atomic<int> note {-1};
    std::atomic<float> velocity {0.0f};
    std::atomic<float> gain {0.5f};
    std::atomic<bool> releaseRequested {false};
};
