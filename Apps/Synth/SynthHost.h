#pragma once

#include "Synth.h"

#include <MakeASound/MakeASound.h>

#include <algorithm>
#include <utility>

// The device and MIDI glue around the Synth. The synth is declared before the
// engine so that it outlives the stream the engine stops on destruction.
struct SynthHost
{
    explicit SynthHost(Synth::MidiAppliedCallback onMidiApplied)
    {
        synth.midiAppliedCb = std::move(onMidiApplied);
        config = manager.getDefaultOutputConfig();
        engine.start(config, synth);
    }

    void applySampleRate(int rate)
    {
        config.sampleRate = rate;
        engine.start(config, synth);
    }

    void applyBlockSize(int size)
    {
        config.maxBlockSize = size;
        engine.start(config, synth);
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

            engine.start(config, synth);
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

    MS::DeviceManager manager;
    MS::MidiManager midi;
    Synth synth;
    MS::Engine engine {manager, midi};
    MS::StreamConfig config;
};
