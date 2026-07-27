// Tests for MakeASound::SPSCQueue - the bounded, wait-free single-producer /
// single-consumer queue. The single-threaded cases pin the FIFO / full / empty /
// wrap-around semantics; the concurrent cases are the point of the exercise:
// they run a real producer thread against a real consumer thread and prove that
// every element crosses intact, exactly once, and in order.

#include <MakeASound/Realtime/SPSCQueue.h>

#include <NanoTest/NanoTest.h>

#include <atomic>
#include <thread>

using namespace nano;
using MakeASound::SPSCQueue;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
// A payload with an internal invariant (tag == derive(seq)). It lets the
// concurrent tests catch a torn or half-published element - not just a lost or
// reordered one - because the whole struct must cross the release/acquire fence
// as a unit for the invariant to hold on the far side.
struct Payload
{
    int seq = 0;
    int tag = 0;
};

int derive(int seq) noexcept
{
    return seq * 3 + 7;
}

// ---------------------------------------------------------------------------
// Single-threaded semantics
// ---------------------------------------------------------------------------

auto tFifoOrder = test("SPSCQueue/deliversInFifoOrder") = []
{
    auto queue = SPSCQueue<int, 8> {};

    check(queue.push(10));
    check(queue.push(20));
    check(queue.push(30));

    auto value = 0;
    check(queue.pop(value) && value == 10);
    check(queue.pop(value) && value == 20);
    check(queue.pop(value) && value == 30);
};

auto tPopEmpty = test("SPSCQueue/popOnEmptyReturnsFalseAndLeavesOutputAlone") = []
{
    auto queue = SPSCQueue<int, 4> {};

    auto value = -123;
    check(!queue.pop(value));
    check(value == -123); // contract: out is untouched when empty
};

auto tFull = test("SPSCQueue/pushOnFullReturnsFalseUntilRoomIsMade") = []
{
    // Capacity 3 means exactly three elements fit at once.
    auto queue = SPSCQueue<int, 3> {};

    check(queue.push(1));
    check(queue.push(2));
    check(queue.push(3));
    check(!queue.push(4)); // full - dropped

    auto value = 0;
    check(queue.pop(value) && value == 1);

    check(queue.push(4)); // one slot freed, so this fits now
    check(!queue.push(5)); // and full again
};

auto tWrapAround = test("SPSCQueue/wrapsAroundTheRingWithoutLosingOrder") = []
{
    // Small capacity plus an unbalanced push/pop rhythm marches the read and
    // write cursors around the ring many times, exercising the modulo wrap.
    auto queue = SPSCQueue<int, 4> {};

    auto next = 0; // next value to push
    auto expect = 0; // next value we expect to pop

    for (auto round = 0; round < 1000; ++round)
    {
        for (auto k = 0; k < 3; ++k)
            if (queue.push(next))
                ++next;

        for (auto k = 0; k < 2; ++k)
        {
            auto value = 0;
            if (queue.pop(value))
            {
                check(value == expect);
                ++expect;
            }
        }
    }

    // Drain whatever is still buffered; it must continue the same sequence.
    auto value = 0;
    while (queue.pop(value))
    {
        check(value == expect);
        ++expect;
    }

    check(expect == next); // everything pushed was eventually popped
    check(next > 4); // sanity: we really did wrap past the buffer
};

// ---------------------------------------------------------------------------
// Concurrent producer / consumer
// ---------------------------------------------------------------------------

// Core contention test: one producer thread, one consumer thread, a large run of
// elements pushed through a modest queue so both the "full" and "empty" paths are
// hit constantly. The consumer records any violation into `ok` rather than
// calling check() off the main thread; we assert once, after the join.
auto tConcurrent = test("SPSCQueue/concurrentProducerConsumerDeliversEverythingInOrder") = []
{
    constexpr auto total = 1'000'000;
    auto queue = SPSCQueue<Payload, 1024> {};

    std::atomic<bool> ok {true};
    std::atomic<int> receivedCount {0};

    auto consumer = std::thread(
        [&]
        {
            auto expected = 0;
            auto item = Payload {};

            while (expected < total)
            {
                if (queue.pop(item))
                {
                    // In order (catches reorder / loss / duplication) and intact
                    // (catches a torn or half-visible publish).
                    if (item.seq != expected || item.tag != derive(item.seq))
                        ok.store(false, std::memory_order_relaxed);

                    ++expected;
                }
                // else: empty, spin until the producer catches up.
            }

            receivedCount.store(expected, std::memory_order_relaxed);
        });

    // This thread is the single producer. Spinning on a full queue is the
    // backpressure that keeps producer and consumer rate-matched.
    for (auto i = 0; i < total; ++i)
    {
        auto item = Payload {i, derive(i)};
        while (!queue.push(item))
            ; // full - wait for the consumer to drain a slot
    }

    consumer.join();

    check(ok.load(std::memory_order_relaxed));
    check(receivedCount.load(std::memory_order_relaxed) == total);
};

// The extreme of the same idea: capacity 1 means at most one element is ever in
// flight, so almost every element is handed over right at the full/empty
// boundary - maximum contention on exactly the indices that tell full from empty.
auto tConcurrentTiny = test("SPSCQueue/concurrentWithCapacityOneStillOrdersEverything") = []
{
    constexpr auto total = 200'000;
    auto queue = SPSCQueue<Payload, 1> {};

    std::atomic<bool> ok {true};
    std::atomic<int> receivedCount {0};

    auto consumer = std::thread(
        [&]
        {
            auto expected = 0;
            auto item = Payload {};

            while (expected < total)
            {
                if (queue.pop(item))
                {
                    if (item.seq != expected || item.tag != derive(item.seq))
                        ok.store(false, std::memory_order_relaxed);

                    ++expected;
                }
            }

            receivedCount.store(expected, std::memory_order_relaxed);
        });

    for (auto i = 0; i < total; ++i)
    {
        auto item = Payload {i, derive(i)};
        while (!queue.push(item))
            ;
    }

    consumer.join();

    check(ok.load(std::memory_order_relaxed));
    check(receivedCount.load(std::memory_order_relaxed) == total);
};
} // namespace
