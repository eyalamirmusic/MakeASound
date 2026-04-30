#pragma once

namespace MakeASound::Algorithms
{

// Stable insertion sort over a container with random-access `begin()` /
// `end()`. Allocation-free and noexcept provided the element type's copy
// and the comparator are themselves noexcept — suitable for audio-thread
// use on small or mostly-ordered ranges. O(N) for already-sorted input,
// O(N^2) worst case.
template <class Container, class Compare>
void stableInsertionSort(Container& c, Compare less) noexcept
{
    auto first = c.begin();
    auto last = c.end();

    for (auto it = first + 1; it < last; ++it)
    {
        auto key = *it;
        auto slot = it;
        while (slot > first && less(key, *(slot - 1)))
        {
            *slot = *(slot - 1);
            --slot;
        }
        *slot = key;
    }
}

// Overload that defers to the element type's `operator<`, mirroring
// std::sort's no-comparator form.
template <class Container>
void stableInsertionSort(Container& c) noexcept
{
    stableInsertionSort(c,
                        [](const auto& a, const auto& b) noexcept { return a < b; });
}

} // namespace MakeASound::Algorithms
