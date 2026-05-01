#include "MidiBlockSync.h"
#include "../MidiManager/MidiManager.h"

#include <algorithm>
#include <chrono>

namespace MakeASound
{

void MidiBlockSync::drainForBlock(MidiManager& midi, int numSamples, int sampleRate)
{
    buffer.clear();

    if (numSamples <= 0 || sampleRate <= 0)
        return;

    auto now = std::chrono::steady_clock::now();
    midi.drainMessages(buffer);

    if (!hasPrevBlock)
    {
        prevBlockStart = now;
        hasPrevBlock = true;

        for (auto& evt: buffer)
            evt.event.sampleOffset = 0;

        return;
    }

    auto maxOffset = numSamples - 1;
    auto rate = static_cast<double>(sampleRate);

    for (auto& evt: buffer)
    {
        auto delta =
            std::chrono::duration<double>(evt.arrival - prevBlockStart).count();
        auto offset = static_cast<int>(delta * rate);
        evt.event.sampleOffset = std::clamp(offset, 0, maxOffset);
    }

    prevBlockStart = now;
}

void MidiBlockSync::reset() noexcept
{
    buffer.clear();
    hasPrevBlock = false;
}

} // namespace MakeASound
