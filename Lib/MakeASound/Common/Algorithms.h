#pragma once

namespace MakeASound::Algorithms
{

// Allocation-free, so it is safe on the audio thread; the unconditional
// noexcept assumes the element copy and the comparator never throw.
// O(N) for already-sorted input, O(N^2) worst case.
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

template <class Container>
void stableInsertionSort(Container& c) noexcept
{
    stableInsertionSort(c,
                        [](const auto& a, const auto& b) noexcept { return a < b; });
}

} // namespace MakeASound::Algorithms
