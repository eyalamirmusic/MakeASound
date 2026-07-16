#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace MakeASound
{

// A bounded, wait-free single-producer / single-consumer queue that delivers
// *every* element in FIFO order.
//
// This is the counterpart to EA::Fifo, and the distinction matters: EA::Fifo
// keeps only the most recent value and is happy to drop the updates in between
// (great for a parameter snapshot — "latest gain wins"). This queue drops
// nothing (until it's full), which is what you want for discrete *events* —
// "start this sound", "this note went down" — where a dropped element is a
// dropped event, not just a stale value.
//
// Contract: exactly one thread calls push(), exactly one (other) thread calls
// pop(). That single-producer / single-consumer split is what lets both sides
// be lock-free with plain acquire/release fences and no CAS loop. Both calls are
// wait-free and allocation-free, so either end is safe on an audio thread.
//
// `Capacity` is how many elements can be in flight at once; one extra slot is
// held back internally to tell "full" from "empty", so the backing array is
// Capacity + 1. `T` is stored by value and assigned across the fence, so it
// should be trivially copyable (a small POD command, typically).
template <typename T, std::size_t Capacity>
class SPSCQueue
{
public:
    static_assert(Capacity >= 1, "SPSCQueue needs room for at least one element");

    // Producer thread only. Copies `item` in and publishes it. Returns false
    // and drops the item if the queue is full.
    bool push(const T& item) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto next = increment(write);

        // Full: advancing would land on the slot the consumer is about to read.
        if (next == readIndex.load(std::memory_order_acquire))
            return false;

        buffer[write] = item;

        // Release pairs with the consumer's acquire on writeIndex, so it sees
        // the buffer write once it sees the advanced index.
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    // Consumer thread only. Copies the oldest element into `out` and returns
    // true, or returns false (leaving `out` untouched) if the queue is empty.
    bool pop(T& out) noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);

        // Empty: the consumer has caught up with the producer.
        if (read == writeIndex.load(std::memory_order_acquire))
            return false;

        out = buffer[read];

        // Release pairs with the producer's acquire on readIndex, so the slot
        // isn't seen as free until we've finished reading it.
        readIndex.store(increment(read), std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t bufferSize = Capacity + 1;

    static std::size_t increment(std::size_t index) noexcept
    {
        return (index + 1) % bufferSize;
    }

    std::array<T, bufferSize> buffer {};
    std::atomic<std::size_t> writeIndex {0};
    std::atomic<std::size_t> readIndex {0};
};

} // namespace MakeASound
