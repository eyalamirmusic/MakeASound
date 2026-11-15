#pragma once

#include "../Common/Common.h"

namespace MakeASound
{

// One host automation point, by the parameter's index in the plugin's list. A
// step when rampSamples is 0; otherwise a line whose sample sampleOffset + k
// (0 <= k <= rampSamples) holds previousValue + (value - previousValue) * k /
// rampSamples, so it may end past the block. Values are normalized.
struct ParameterChange
{
    // Its value at `sample`, which must not lie before sampleOffset.
    float valueAt(int sample) const noexcept;

    int index = 0;
    int sampleOffset = 0;
    float value = 0.f;
    int rampSamples = 0;
    float previousValue = 0.f;
};

// A block's automation, sorted by offset, each change carrying both its ends.
// Reserved ahead of the block and never grown on the audio thread.
struct ParameterChanges : Vector<ParameterChange>
{
    // Stable and allocation-free, as MIDI::Buffer::sortByOffset().
    void sortByOffset() noexcept;

    // Parameter `index` at `sample`, from a sorted list: `fallback` when no
    // change is for it.
    float valueAt(int index, int sample, float fallback) const noexcept;
};

} // namespace MakeASound
