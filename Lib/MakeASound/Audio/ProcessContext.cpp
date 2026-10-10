#include "ProcessContext.h"

namespace MakeASound
{

void ProcessContext::prepare(const BusLayout& layout, int midiCapacity)
{
    inputs.resize(layout.inputs.size());
    outputs.resize(layout.outputs.size());
    midiIn.resize(layout.midiInputs.size());
    midiOut.resize(layout.midiOutputs.size());

    for (auto& buffer: midiIn)
        buffer.reserveAtLeast(midiCapacity);

    for (auto& buffer: midiOut)
        buffer.reserveAtLeast(midiCapacity);

    detachedMidiIn.reserveAtLeast(midiCapacity);
    detachedMidiOut.reserveAtLeast(midiCapacity);
    clearMidi();
}

void ProcessContext::clearMidi() noexcept
{
    for (auto& buffer: midiIn)
        buffer.clear();

    for (auto& buffer: midiOut)
        buffer.clear();

    detachedMidiIn.clear();
    detachedMidiOut.clear();
}

} // namespace MakeASound
