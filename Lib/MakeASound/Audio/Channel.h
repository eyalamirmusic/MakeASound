#pragma once

#include "../Common/Common.h"

namespace MakeASound
{

// A non-owning view over a single channel's samples.
//
// This is the currency for passing one channel of audio around. It is a
// contiguous range, so range-for (`for (auto& sample : channel)`) and the
// standard algorithms (std::fill, std::copy, std::ranges::fill, ...) work
// directly on it, and it carries EA::Span's own helpers (`fill`, `copyFrom`,
// `mixFrom`, ...) on top.
//
// Sizes and indices are `int`, matching the rest of the library — there are no
// size_t conversions at the call site.
using Channel = Span<float>;

} // namespace MakeASound
