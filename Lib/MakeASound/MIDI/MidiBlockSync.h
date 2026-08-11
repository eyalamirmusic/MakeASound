#pragma once

#include "MidiInfo.h"

namespace MakeASound
{

class MidiManager;

// Resolves queued arrival times into sample offsets in the current block.
// Needs queue-mode inputs (MidiManager::openInput without a callback). Maps
// [prevBlockStart, now) onto [0, numSamples), so events land one block late —
// the only way to keep offsets non-negative when MIDI arrives on its own thread.
class MidiBlockSync
{
public:
    // Call once per audio callback; each call advances the window.
    void drainForBlock(MidiManager& midi, int numSamples, int sampleRate);

    // Call after a stream restart, xrun or config change — any gap that
    // invalidates the previous window.
    void reset() noexcept;

    const MidiEvents& events() const noexcept { return buffer; }
    bool empty() const noexcept { return buffer.empty(); }

private:
    MidiEvents buffer;
    MidiTimePoint prevBlockStart {};
    bool hasPrevBlock {false};
};

} // namespace MakeASound
