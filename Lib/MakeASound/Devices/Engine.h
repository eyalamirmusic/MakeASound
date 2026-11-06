#pragma once

#include "DeviceManager.h"
#include "../Audio/Processor.h"
#include "../MIDI/MidiManager.h"
#include "../MIDI/MidiBlockSync.h"

#include <atomic>

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
// is not audio-thread safe, so a processor that sends hands its events to a
// thread of its own (the standalone format's MidiSender).
//
// Every public member but process() is host-thread only, and so is the
// processor's prepare(): process() never calls it. A stream that turns up in a
// shape the spec does not fit plays silence and asks for a restart instead, which
// the host grants from its own thread (takeRestartRequest(), restart()) on a timer
// it already keeps. StandaloneHost::poll() does it for the standalone format; a
// host that never asks keeps a silent stream, never a crashed one, until it next
// starts it.
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
    // itself. The rate is a device's, so whole (StreamConfig, AudioCallbackInfo);
    // it is the one place a device rate becomes ProcessSpec's double. Drops a
    // pending restart request: the processor now has a spec of its own choosing.
    void prepare(Processor& processor, int sampleRate, int maxBlockSize);

    // prepare() for the config, then the stream. The config's capture side is
    // dropped when the layout declares no input bus, so an instrument never
    // costs the app a microphone permission; an unset block size opens at
    // defaultBlockSize. Restarts a running stream.
    Error start(const StreamConfig& config, Processor& processor);

    // Also drops a pending restart request, so a stream the host stopped stays
    // stopped.
    void stop();

    // True once for each time process() met a block the spec does not fit (see
    // process()); false again until the next one. Poll it from a timer.
    bool takeRestartRequest() noexcept;

    // stop(), prepare() for the shape the stream turned up in (the rate and
    // largest block process() saw, else the config's), and the stream again on
    // the same config. INVALID_USE with no processor bound. What a host does when
    // takeRestartRequest() says so.
    Error restart();

    bool isRunning() const;
    const ProcessSpec& getSpec() const noexcept { return spec; }
    const StreamConfig& getConfig() const noexcept { return config; }

    // The stream's callback. A dirty block resets the processor and the MIDI
    // window. A block the spec does not fit (another rate, or a block or device
    // maximum past the spec's) is never processed: the outputs are cleared, MIDI
    // that arrived is dropped, and a restart is requested. Every block is
    // silenced that way until one fits again, and the first one that does resets
    // the processor. With no processor bound the outputs are cleared.
    void process(AudioCallbackInfo& info) noexcept;

    static constexpr int defaultBlockSize = 512;

private:
    // Everything start() and restart() share once config is set: the processor
    // is prepared at sampleRate/maxBlockSize, the stream opened, and prepared
    // again if the device runs another rate or block size, a block size of no
    // less than minBlockSize.
    Error open(Processor& processorToUse,
               int sampleRate,
               int maxBlockSize,
               int minBlockSize);

    bool fits(const AudioCallbackInfo& info) const noexcept;
    void requestRestart(const AudioCallbackInfo& info) noexcept;
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

    // Written by process() when a block does not fit, read and cleared by the
    // host thread: the request, and the shape restart() prepares for.
    std::atomic<bool> restartPending {false};
    std::atomic<int> pendingSampleRate {0};
    std::atomic<int> pendingBlockSize {0};

    // Audio thread only: the last block was silenced, so the next one that fits
    // follows a gap.
    bool silenced = false;
};

} // namespace MakeASound
