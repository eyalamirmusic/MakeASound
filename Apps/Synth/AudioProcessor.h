#pragma once

#include "Synth.h"

#include <MakeASound/MakeASound.h>
#include <eacp/Core/Core.h>

struct AudioProcessor
{
    using MidiAppliedCallback = std::function<void(const MIDI::Event&)>;

    AudioProcessor()
    {
        config = manager.getDefaultConfig();
        config.input.reset();
        manager.start(config, [this](auto& info) { audioCallback(info); });
    }

    Synth& getSynth() { return synth; }
    const Synth& getSynth() const { return synth; }

    const MS::StreamConfig& getStreamConfig() const { return config; }

    // Fired on the UI thread (auto-bounced from the audio thread) after a
    // MIDI message has been applied to the synth state.
    void setMidiAppliedCallback(MidiAppliedCallback cb)
    { midiAppliedCb = std::move(cb); }

    void applySampleRate(int rate)
    {
        config.sampleRate = rate;
        manager.setConfig(config);
    }

    void applyBlockSize(int size)
    {
        config.maxBlockSize = size;
        manager.setConfig(config);
    }

    bool applyDevice(int deviceId)
    {
        for (auto& device: manager.getDevices())
        {
            if (device.id != deviceId || device.outputChannels == 0)
                continue;

            config.output = MS::StreamParameters {device, false};

            if (!device.sampleRates.empty()
                && std::ranges::find(device.sampleRates, config.sampleRate)
                       == device.sampleRates.end())
                config.sampleRate = device.sampleRates.front();

            manager.setConfig(config);
            return true;
        }

        return false;
    }

    void applyMidiPortToggle(int portId, bool on)
    {
        if (on)
        {
            midi.openInput(portId);
        }
        else
        {
            midi.closeInput(portId);
            synth.releaseAllNotes();
        }
    }

    void audioCallback(MS::AudioCallbackInfo& info)
    {
        if (info.dirty)
        {
            synth.reset();
            midiSync.reset();
        }

        midiSync.drainForBlock(midi, info.numSamples, info.sampleRate);

        auto cursor = 0;

        for (auto& evt: midiSync.events())
        {
            synth.render(info, cursor, evt.event.sampleOffset);
            applyMidiOnAudioThread(evt.event);
            cursor = evt.event.sampleOffset;
        }

        synth.render(info, cursor, info.numSamples);
    }

    void applyMidiOnAudioThread(const MIDI::Event& midiEvent)
    {
        synth.applyMidiEvent(midiEvent);
        // eacp::Threads::callAsync([midiEvent, cb = midiAppliedCb] { cb(midiEvent); });
    }

    Synth synth;
    MS::DeviceManager manager;
    MS::MidiManager midi;
    MS::MidiBlockSync midiSync;
    MS::StreamConfig config;
    MidiAppliedCallback midiAppliedCb;
};
