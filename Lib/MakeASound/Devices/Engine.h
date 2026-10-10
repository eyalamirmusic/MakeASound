#pragma once

#include "DeviceManager.h"
#include "../Audio/Processor.h"
#include "../MIDI/MidiManager.h"
#include "../MIDI/MidiBlockSync.h"

namespace MakeASound
{

// Runs a Processor on a DeviceManager's stream with a MidiManager's open inputs
// feeding its main MIDI bus (queue mode only: an input opened with a callback is
// delivered there, not here). The processor's main input bus reads the device's
// capture channels and its main output bus writes the playback channels; a bus
// channel the device has no side for reads silence or writes into a bin, and a
// device channel past the bus is cleared. Nothing is copied: the context's
// buffers refer straight into the callback's.
//
// MIDI output buses are cleared every block and otherwise ignored here; sending
// is not audio-thread safe and arrives with the standalone format.
//
// Every public member but process() is host-thread only.
class Engine
{
public:
    Engine(DeviceManager& devices, MidiManager& midi);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Binds the processor, sizes the context and the stand-in buffers to its
    // layout and the block size, and calls its prepare(). What start() does
    // before opening the stream, on its own for a host that drives process()
    // itself.
    void prepare(Processor& processor, int sampleRate, int maxBlockSize);

    // prepare() for the config, then the stream. The config's capture side is
    // dropped when the layout declares no input bus, so an instrument never
    // costs the app a microphone permission; an unset block size opens at
    // defaultBlockSize. Restarts a running stream.
    Error start(const StreamConfig& config, Processor& processor);
    void stop();

    bool isRunning() const;
    const ProcessSpec& getSpec() const noexcept { return spec; }
    const StreamConfig& getConfig() const noexcept { return config; }

    // The stream's callback. A dirty block resets the processor and the MIDI
    // window; one whose rate or block size differs from the spec re-prepares
    // first, on the audio thread, because a re-opened device is the one moment
    // the stream has already gapped. With no processor bound the outputs are
    // cleared.
    void process(AudioCallbackInfo& info) noexcept;

    static constexpr int defaultBlockSize = 512;

private:
    void bindBuses(AudioCallbackInfo& info) noexcept;

    DeviceManager& devices;
    MidiManager& midi;

    Processor* processor = nullptr;
    ProcessSpec spec;
    StreamConfig config;
    ProcessContext context;
    MidiBlockSync midiSync;

    // The tables the main buses refer through, rebuilt per block from the
    // callback's channels and the stand-ins below.
    Vector<float*> inputTable;
    Vector<float*> outputTable;

    // Silence for an input channel the device cannot feed; a bin for an output
    // channel it cannot play. One channel each, sized to the block.
    Buffer silence;
    Buffer bin;
};

} // namespace MakeASound
