// Tests for RealtimeSwap: what one thread publishes another sees at its next
// block, and what the audio thread lets go of is freed on the message thread -
// which these cases pump the way a host's event loop would.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <atomic>
#include <thread>

using namespace nano;

using MakeASound::RealtimeSwap;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
struct Counts
{
    std::atomic<int> live {0};
    std::atomic<int> freedOffMessageThread {0};
};

struct Tracked
{
    Tracked(Counts& countsToUse, int valueToUse)
        : counts(countsToUse)
        , value(valueToUse)
    {
        ++counts.live;
    }

    ~Tracked()
    {
        if (!MakeASound::isMessageThread())
            ++counts.freedOffMessageThread;

        --counts.live;
    }

    Counts& counts;
    int value;
};

template <typename Predicate>
bool pumpUntil(Predicate ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {5000});
}

void pumpFor(int ms)
{
    eacp::Threads::runEventLoopFor(eacp::Time::MS {ms});
}

auto tNextBlockSeesPublish = test("RealtimeSwap/nextBlockSeesWhatWasPublished") = []
{
    auto counts = Counts();
    auto swap = RealtimeSwap<Tracked>();

    check(swap.currentForBlock() == nullptr);

    swap.publish(counts, 1);
    check(swap.currentForBlock()->value == 1);
    check(swap.currentForBlock()->value == 1);

    auto publisher = std::thread([&] { swap.publish(counts, 2); });
    publisher.join();

    check(swap.currentForBlock()->value == 2);
};

auto tReclaimedOnMessageThread =
    test("RealtimeSwap/previousIsFreedOnTheMessageThreadAndNotBefore") = []
{
    auto counts = Counts();
    auto swap = RealtimeSwap<Tracked>();

    swap.publish(counts, 1);
    auto* first = swap.currentForBlock();

    swap.publish(counts, 2);
    pumpFor(400);
    check(counts.live == 2);
    check(first->value == 1);

    check(swap.currentForBlock()->value == 2);
    check(counts.live == 2);

    check(pumpUntil([&] { return counts.live == 1; }));
    check(counts.freedOffMessageThread == 0);
    check(swap.currentForBlock()->value == 2);
};

auto tUnseenPublishIsFreed =
    test("RealtimeSwap/aPublishTheAudioNeverTookIsFreed") = []
{
    auto counts = Counts();
    auto swap = RealtimeSwap<Tracked>();

    swap.publish(counts, 1);
    swap.publish(counts, 2);
    check(counts.live == 1);

    check(swap.currentForBlock()->value == 2);
};

auto tBurst = test("RealtimeSwap/aBurstOfPublishesLosesNothing") = []
{
    constexpr auto publishes = 2000;

    auto counts = Counts();
    auto swap = RealtimeSwap<Tracked>();
    auto done = std::atomic<bool> {false};
    auto lastSeen = std::atomic<int> {0};
    auto wentBackwards = std::atomic<bool> {false};

    auto audio = std::thread(
        [&]
        {
            auto finish = [&]
            {
                auto* object = swap.currentForBlock();
                auto value = object != nullptr ? object->value : 0;

                if (value < lastSeen)
                    wentBackwards = true;

                lastSeen = value;
            };

            while (!done)
                finish();

            finish();
        });

    for (auto i = 1; i <= publishes; ++i)
    {
        swap.publish(counts, i);

        if (i % 100 == 0)
            pumpFor(1);
    }

    // The retired slot may still be full, delaying the last swap by a sweep.
    check(pumpUntil(
        [&]
        {
            swap.publish(counts, publishes);
            return lastSeen == publishes;
        }));

    done = true;
    audio.join();

    check(!wentBackwards);

    // The audio thread has gone, so this one takes its place to drain the slots.
    check(pumpUntil(
        [&]
        {
            swap.currentForBlock();
            return counts.live == 1;
        }));
    check(counts.freedOffMessageThread == 0);
};

auto tTeardown = test("RealtimeSwap/teardownFreesEverySlot") = []
{
    auto counts = Counts();

    {
        auto swap = RealtimeSwap<Tracked>();

        swap.publish(counts, 1);
        swap.currentForBlock();
        swap.publish(counts, 2);
        swap.currentForBlock();
        swap.publish(counts, 3);

        check(counts.live == 3);
    }

    check(counts.live == 0);
    pumpFor(300);
    check(counts.live == 0);
};

struct HoldsASwap
{
    explicit HoldsASwap(bool& destroyedToUse)
        : destroyed(destroyedToUse)
    {
    }

    ~HoldsASwap()
    {
        auto inner = RealtimeSwap<int>();
        inner.publish(1);
        destroyed = true;
    }

    bool& destroyed;
};

auto tReentrantTeardown =
    test("RealtimeSwap/freeingAnObjectThatOwnsASwapDoesNotDeadlock") = []
{
    auto destroyed = false;
    auto swap = RealtimeSwap<HoldsASwap>();

    swap.publish(destroyed);
    swap.currentForBlock();
    swap.publish(destroyed);
    swap.currentForBlock();

    while (MakeASound::reclaimNow() > 0)
    {
    }

    check(destroyed);
};
} // namespace
