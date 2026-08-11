#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace MakeASound
{

// Bounded FIFO: exactly one thread pushes, one other pops. That contract is
// what keeps both ends wait-free and allocation-free (safe on an audio thread)
// with plain acquire/release and no CAS loop. T is assigned across the fence,
// so keep it a small POD. One slot is held back to tell full from empty.
template <typename T, std::size_t Capacity>
class SPSCQueue
{
public:
    static_assert(Capacity >= 1, "SPSCQueue needs room for at least one element");

    // Producer thread only. Returns false and drops the item when full.
    bool push(const T& item) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto next = increment(write);

        if (next == readIndex.load(std::memory_order_acquire))
            return false;

        buffer[write] = item;

        // Release pairs with the consumer's acquire: whoever sees the new
        // index also sees the slot's contents.
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    // Consumer thread only. Leaves `out` untouched when empty.
    bool pop(T& out) noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);

        if (read == writeIndex.load(std::memory_order_acquire))
            return false;

        out = buffer[read];

        // Release pairs with the producer's acquire: the slot is not seen as
        // free until we have finished reading it.
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
