#pragma once

#include <algorithm>
#include <iterator>
#include <utility>

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

    // Not first + 1: on an empty container that is past the end, which a
    // checked iterator refuses.
    if (first == last)
        return;

    for (auto it = std::next(first); it != last; ++it)
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

// Stable and O(N log N), merging through `scratch`; allocation-free when scratch
// already has the capacity, an insertion sort otherwise. Bottom-up, so the
// elements must be cheap to copy.
template <class Container, class Compare>
void stableMergeSort(Container& c, Container& scratch, Compare less) noexcept
{
    auto size = c.size();

    if (size < 2)
        return;

    if (scratch.capacity() < size)
    {
        stableInsertionSort(c, less);
        return;
    }

    scratch.resize(size);
    auto* from = c.data();
    auto* to = scratch.data();

    for (auto width = 1; width < size; width *= 2)
    {
        for (auto start = 0; start < size; start += 2 * width)
        {
            auto mid = std::min(start + width, size);
            auto end = std::min(start + 2 * width, size);
            auto left = start;
            auto right = mid;
            auto out = start;

            while (left < mid && right < end)
                to[out++] =
                    less(from[right], from[left]) ? from[right++] : from[left++];

            while (left < mid)
                to[out++] = from[left++];

            while (right < end)
                to[out++] = from[right++];
        }

        std::swap(from, to);
    }

    if (from != c.data())
        std::copy_n(from, size, c.data());
}

template <class Container>
void stableInsertionSort(Container& c) noexcept
{
    stableInsertionSort(c,
                        [](const auto& a, const auto& b) noexcept { return a < b; });
}

} // namespace MakeASound::Algorithms
