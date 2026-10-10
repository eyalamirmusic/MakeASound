#pragma once

#include "Buffer.h"
#include "BusLayout.h"
#include "Playhead.h"
#include "../MIDI/MIDI.h"

namespace MakeASound
{

// What a Processor is prepared for. Sample rates are int across the library, as
// in DeviceManager; a plugin adapter rounds at its boundary.
struct ProcessSpec
{
    bool operator==(const ProcessSpec&) const = default;

    int sampleRate = 0;
    int maxBlockSize = 0;
    BusLayout layout;
};

// Sample-accurate automation lands here in a later stage; empty for now so that
// adding it changes nothing a Processor already compiles against.
struct ParameterChanges
{
};

// One block's worth of buses. The audio buffers refer to memory the host owns
// (a device callback's scratch, a plugin host's channel arrays); the MIDI buffers
// and the vectors are reserved by prepare() and never grow after it.
//
// The main*() accessors hand back bus 0, or a stand-in for a bus the layout did
// not declare: an empty Buffer for audio, a real MIDI::Buffer that reads empty and
// swallows writes for MIDI. So code written against mainOutput() runs unchanged
// under a layout with no output, and reaching an undeclared bus is an empty
// block rather than an out-of-bounds read.
struct ProcessContext
{
    static constexpr int defaultMidiCapacity = 1024;

    // Host thread. Sizes the bus vectors to the layout and reserves every MIDI
    // buffer, the stand-ins included.
    void prepare(const BusLayout& layout, int midiCapacity = defaultMidiCapacity);

    // Every MIDI buffer, in and out, stand-ins included.
    void clearMidi() noexcept;

    // The output half of clearMidi(), stand-in included.
    void clearMidiOut() noexcept;

    const Buffer& mainInput() const noexcept
    {
        return inputs.empty() ? detachedInput : inputs[0];
    }

    Buffer& mainOutput() noexcept
    {
        return outputs.empty() ? detachedOutput : outputs[0];
    }

    MIDI::Buffer& mainMidiIn() noexcept
    {
        return midiIn.empty() ? detachedMidiIn : midiIn[0];
    }

    MIDI::Buffer& mainMidiOut() noexcept
    {
        return midiOut.empty() ? detachedMidiOut : midiOut[0];
    }

    Vector<Buffer> inputs;
    Vector<Buffer> outputs;
    Vector<MIDI::Buffer> midiIn;
    Vector<MIDI::Buffer> midiOut;

    Playhead playhead;
    ParameterChanges paramChanges;

private:
    Buffer detachedInput;
    Buffer detachedOutput;
    MIDI::Buffer detachedMidiIn;
    MIDI::Buffer detachedMidiOut;
};

} // namespace MakeASound
