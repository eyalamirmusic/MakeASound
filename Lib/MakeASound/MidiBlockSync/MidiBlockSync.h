#pragma once

#include "../MidiInfo/MidiInfo.h"

namespace MakeASound
{

class MidiManager;

// Drains queued MIDI events from a MidiManager and resolves each event's
// wall-clock arrival time into a sample offset within the current audio
// block. Designed to be called from inside the audio callback.
//
// Use queue-mode inputs (`MidiManager::openInput(portId)` without a callback)
// so events are buffered rather than fired on RtMidi's input thread.
//
// Timing model: events that physically arrive during block N-1 are placed
// in block N. The sync internally remembers the previous callback's start
// time and uses [prevBlockStart, now) as the window mapped onto
// [0, numSamples). This costs one audio block of MIDI latency but is the
// only way to produce strictly non-negative sample offsets when MIDI is
// queued on a separate input thread.
class MidiBlockSync
{
public:
    // Call once per audio callback. Pulls queued events from `midi` and
    // assigns each a sample offset in [0, numSamples).
    void drainForBlock(MidiManager& midi, int numSamples, int sampleRate);

    // Forget the previous block start. Call after a stream restart or any
    // gap that invalidates the previous window (e.g. xrun, config change).
    void reset() noexcept;

    const MidiEvents& events() const noexcept { return buffer; }
    bool empty() const noexcept { return buffer.empty(); }

private:
    MidiEvents buffer;
    MidiTimePoint prevBlockStart {};
    bool hasPrevBlock {false};
};

} // namespace MakeASound
