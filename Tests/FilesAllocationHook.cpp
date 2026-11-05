// The allocation watch of FilesTestHelpers.h: this executable's replacement of the
// global operator new and delete, which counts what is asked for while a watch is
// on. Portable (no interposition), so it runs on every platform the suites do.
// Only the unaligned forms are replaced; the aligned ones keep their defaults,
// which allocate and free on their own.

#include "FilesTestHelpers.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace FilesAllocationHook
{
std::atomic<bool> watching {false};
std::atomic<std::int64_t> count {0};
std::atomic<std::size_t> largest {0};
std::atomic<std::size_t> total {0};

void* allocate(std::size_t size) noexcept
{
    if (watching.load(std::memory_order_relaxed))
    {
        count.fetch_add(1, std::memory_order_relaxed);
        total.fetch_add(size, std::memory_order_relaxed);
        auto seen = largest.load(std::memory_order_relaxed);

        while (
            size > seen
            && !largest.compare_exchange_weak(seen, size, std::memory_order_relaxed))
        {
        }
    }

    return std::malloc(size == 0 ? 1 : size);
}
} // namespace FilesAllocationHook

namespace FilesTestHelpers
{
void startWatchingAllocations() noexcept
{
    FilesAllocationHook::count = 0;
    FilesAllocationHook::largest = 0;
    FilesAllocationHook::total = 0;
    FilesAllocationHook::watching = true;
}

AllocationStats stopWatchingAllocations() noexcept
{
    FilesAllocationHook::watching = false;
    return {FilesAllocationHook::count.load(),
            FilesAllocationHook::largest.load(),
            FilesAllocationHook::total.load()};
}
} // namespace FilesTestHelpers

void* operator new(std::size_t size)
{
    if (auto* block = FilesAllocationHook::allocate(size))
        return block;

    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return FilesAllocationHook::allocate(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return FilesAllocationHook::allocate(size);
}

void operator delete(void* block) noexcept
{
    std::free(block);
}

void operator delete[](void* block) noexcept
{
    std::free(block);
}

void operator delete(void* block, std::size_t) noexcept
{
    std::free(block);
}

void operator delete[](void* block, std::size_t) noexcept
{
    std::free(block);
}

void operator delete(void* block, const std::nothrow_t&) noexcept
{
    std::free(block);
}

void operator delete[](void* block, const std::nothrow_t&) noexcept
{
    std::free(block);
}
