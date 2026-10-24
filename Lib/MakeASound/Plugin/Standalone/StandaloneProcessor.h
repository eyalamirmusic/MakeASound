#pragma once

#include "MidiSender.h"
#include "../Host/PluginWrapper.h"
#include "../../Realtime/SPSCQueue.h"

#include <atomic>

namespace MakeASound::Standalone
{

// The Processor Engine runs for the standalone format: each block is handed to
// the wrapper through its per-block steps, the way any other format adapter
// drives it, and the plugin's MIDI out goes to the sender thread.
class StandaloneProcessor : public Processor
{
public:
    StandaloneProcessor(PluginWrapper& wrapperToUse, MidiSender& senderToUse);

    BusLayout getBusLayout() const override;
    void prepare(const ProcessSpec& spec) override;
    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    // Message thread (any single non-audio thread): lands at offset 0 of the next
    // block, after the hardware events of the same offset. False when the queue
    // is full.
    bool injectMidi(const MIDI::Event& event) noexcept;

    // Any thread: while set, each block binds silence in place of the device's
    // input channels, so the plugin (and the dry signal an effect passes on)
    // hears nothing of the microphone. Takes effect from the next block.
    void setInputMuted(bool muted) noexcept;
    bool isInputMuted() const noexcept;

private:
    void pushMidiIn(ProcessContext& context) noexcept;
    void bindBuses(ProcessContext& context) noexcept;

    PluginWrapper& wrapper;
    MidiSender& sender;
    SPSCQueue<MIDI::Event, 256> injected;
    std::atomic<bool> inputMuted {false};

    // What a muted block binds: one zeroed channel per input channel, sized in
    // prepare() and cleared again each muted block, since the plugin's input
    // is mutable and a plugin may write through it.
    Buffer silence;

    // One run of channel pointers per bus, back to back: the wrapper keeps
    // referring to an input's table for the whole block.
    Vector<const float*> inputTable;
    Vector<float*> outputTable;
};

} // namespace MakeASound::Standalone
