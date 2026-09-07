#pragma once

// Shared harness for the two allocation suites. ScopedMemoryAllocations interposes
// malloc/calloc/realloc/free and the global new/delete, so what gets counted here is
// heap *activity* on the banned thread - a free trips the probe exactly like a
// malloc does, which is what "this code never touches the allocator" means.
//
// Interposition needs dlsym(RTLD_NEXT, ...), so this header and the two suites that
// use it are only in the build on Apple and Linux - Tests/CMakeLists.txt is what
// says so. A suite that reports zero allocations because nothing was watching is
// worse than no suite.

#include <ScopedMemoryAllocations/Allocations.h>

#include <atomic>

namespace Probe
{

// Runs `fn` with the heap banned on this thread and returns how many times it was
// reached for anyway. Assertions belong outside `fn` - NanoTest's check() formats,
// and formatting allocates.
template <class Fn>
int allocationsIn(Fn&& fn)
{
    auto count = 0;

    EA::Allocations::setViolationHandler([&count] { ++count; });

    {
        auto ban = EA::Allocations::ScopedSetter {};
        fn();
    }

    EA::Allocations::setViolationHandler({});

    return count;
}

// The same measurement on a thread we don't own - an audio callback, RtMidi's input
// thread. The ban is thread_local and those threads run between our calls, so it is
// raised and lowered from inside their own callbacks and the count crosses back
// through an atomic. Install it before the thread starts and keep it installed until
// after the thread is gone: the handler is a plain global.
struct ThreadProbe
{
    ThreadProbe() { EA::Allocations::setViolationHandler([this] { ++violations; }); }

    // A no-op rather than the asserting default: a thread that stopped calling back
    // is still carrying our ban, and its teardown must not abort the suite.
    ~ThreadProbe() { EA::Allocations::setViolationHandler([] {}); }

    ThreadProbe(const ThreadProbe&) = delete;
    ThreadProbe& operator=(const ThreadProbe&) = delete;

    // Called from the foreign thread, once per callback: it bans the heap for the
    // rest of that callback and the whole of the next one, so a steady-state block
    // is measured end to end rather than only the part we can wrap in a scope.
    void mark() noexcept
    {
        ++visits;
        EA::Allocations::setAllowedToAllocate(!measuring.load());
    }

    std::atomic<bool> measuring {true};
    std::atomic<int> visits {0};
    std::atomic<int> violations {0};
};

} // namespace Probe
