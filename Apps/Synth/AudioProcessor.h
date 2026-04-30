#pragma once

#include "Synth.h"

#include <MakeASound/MakeASound.h>
#include <eacp/Core/Core.h>

struct AudioProcessor
{
    using MidiAppliedCallback =
        std::function<void(const MakeASound::MidiInputEvent&)>;

    AudioProcessor()
    {
        config = manager.getDefaultConfig();
        config.input.reset();
        manager.start(config, [this](auto& info) { audioCallback(info); });
    }

    Synth& getSynth() { return synth; }
    const Synth& getSynth() const { return synth; }

    const MakeASound::StreamConfig& getStreamConfig() const { return config; }

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

    // Returns true if the device was found and applied.
    bool applyDevice(int deviceId)
    {
        for (auto& device: manager.getDevices())
        {
            if (device.id != deviceId || device.outputChannels == 0)
                continue;

            config.output = MakeASound::StreamParameters {device, false};

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

    void audioCallback(MakeASound::AudioCallbackInfo& info)
    {
        if (info.dirty)
        {
            synth.reset();
            midiSync.reset();
        }

        auto blockStart = std::chrono::steady_clock::now();
        midiSync.drainForBlock(midi, info.numSamples, info.sampleRate);

        auto cursor = 0;

        for (auto& evt: midiSync.events())
        {
            synth.render(info, cursor, evt.sampleOffset);
            applyMidiOnAudioThread(evt, blockStart, info.sampleRate);
            cursor = evt.sampleOffset;
        }

        synth.render(info, cursor, info.numSamples);
    }

    void applyMidiOnAudioThread(const MakeASound::MidiInputEvent& evt,
                                MakeASound::MidiTimePoint blockStart,
                                int sampleRate)
    {
        synth.applyMidiMessage(evt.message);
        logEventDiagnostic(evt, blockStart, sampleRate);

        if (midiAppliedCb)
            eacp::Threads::callAsync(
                [this, evt]()
                {
                    if (midiAppliedCb)
                        midiAppliedCb(evt);
                });
    }

    static void logEventDiagnostic(const MakeASound::MidiInputEvent& evt,
                                   MakeASound::MidiTimePoint blockStart,
                                   int sampleRate)
    {
        using namespace std::chrono;

        auto arrivalUs =
            duration_cast<microseconds>(evt.arrival - blockStart).count();
        auto applicationUs = static_cast<long long>(
            (1'000'000.0 * evt.sampleOffset) / static_cast<double>(sampleRate));
        auto latencyUs = applicationUs - arrivalUs;

        std::printf("[synth] sampleOffset=%4d  arrival=%+7lldus  applied=%+7lldus  "
                    "latency=%+7lldus  bytes=%zu\n",
                    evt.sampleOffset,
                    static_cast<long long>(arrivalUs),
                    applicationUs,
                    latencyUs,
                    evt.message.bytes.size());
        std::fflush(stdout);
    }

    Synth synth;
    MakeASound::DeviceManager manager;
    MakeASound::MidiManager midi;
    MakeASound::MidiBlockSync midiSync;
    MakeASound::StreamConfig config;
    MidiAppliedCallback midiAppliedCb;
};
