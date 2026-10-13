#pragma once

#include "../../Audio/Buffer.h"
#include "../../Common/Common.h"
#include "../../Realtime/SpinLock.h"

namespace MakeASound
{

// The default bypass's latency compensation: the main input into a ring per
// channel and out again `delay` samples later, so a bypassed plugin lines up with
// the latency it reports. Sized off the audio thread; the audio thread never
// allocates and never waits, and a block that finds a resize under way is silent.
class BypassDelay
{
public:
    BypassDelay() = default;

    BypassDelay(const BypassDelay&) = delete;
    BypassDelay& operator=(const BypassDelay&) = delete;

    // Off the audio thread. Room for `delaySamples` plus a block; silent to start.
    void prepare(int numChannels, int maxBlockSize, int delaySamples);

    // Off the audio thread: reallocates, silent again, only when the ring cannot
    // hold `delaySamples`. A no-op before prepare().
    void ensureDelay(int delaySamples);

    // The longest delay the ring holds; 0 before prepare().
    int capacity() const noexcept;

    // Audio thread: the ring falls silent.
    void clear() noexcept;

    // Audio thread: `input` into the ring, for a block whose output is made
    // elsewhere, so the ring stays current for the next bypassed one.
    void push(const Buffer& input) noexcept;

    // Audio thread: `input` into the ring and the input of `delay` samples ago into
    // `output`, which may alias it. A mono input feeds every output channel;
    // otherwise an output channel past the input's is zeroed. A delay longer than
    // capacity() is clamped to it; before prepare() the delay is 0.
    void process(const Buffer& input, Buffer& output, int delay) noexcept;

private:
    float* channel(int index) noexcept { return ring.data() + index * ringSize; }

    void write(const Buffer& input, int start, int length) noexcept;
    void read(Buffer& output,
              int start,
              int length,
              int delay,
              int inputChannels) noexcept;
    static void copyAcross(const Buffer& input, Buffer& output) noexcept;

    EA::Locks::PrimitiveSpinLock lock;
    Vector<float> ring;
    int numChannels = 0;
    int ringSize = 0;
    int blockSize = 0;
    int writePosition = 0;
};

} // namespace MakeASound
