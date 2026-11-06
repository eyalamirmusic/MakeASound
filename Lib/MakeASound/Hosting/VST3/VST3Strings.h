#pragma once

// Internal to the VST3 hosting TUs: UTF-8 to and from the SDK's UTF-16 strings (a
// Steinberg::Vst::TChar is a char16_t), in place of the SDK's StringConvert. That
// one throws std::range_error on malformed UTF-8 or a lone surrogate, and reads a
// String128 to a terminator wherever it is. Here each malformed sequence becomes
// U+FFFD, a string is read no further than its size, and nothing throws but
// std::bad_alloc. The text comes from a third-party plugin or a user's typing.
//
// No SDK header. The plugin side's copy of this (Plugin/VST3/Text.h) is
// MakeASoundVST3's, which this library does not link.

#include <cstddef>
#include <string>
#include <string_view>

namespace MakeASound::Hosting::VST3Strings
{

// Steinberg::Vst::String128's size, for an SDK signature that decays one to a
// pointer.
inline constexpr auto string128Size = 128;

// At most maxUnits units of text, fewer at a terminator, as UTF-8. A lone
// surrogate reads as U+FFFD, and null as empty.
std::string toUtf8(const char16_t* text, int maxUnits);

// A fixed-size SDK string (a String128, a PClassInfoW field), which may fill its
// array with no terminator.
template <std::size_t Size>
std::string toUtf8(const char16_t (&text)[Size])
{
    return toUtf8(text, static_cast<int>(Size));
}

// UTF-8 as UTF-16, each malformed sequence (Unicode's maximal subpart) a U+FFFD.
std::u16string toUtf16(std::string_view utf8);

// UTF-8 into a buffer of `capacity` units, always terminated: truncated to
// capacity - 1 units, never between the two halves of a pair.
void copyTo(char16_t* destination, int capacity, std::string_view utf8);

} // namespace MakeASound::Hosting::VST3Strings
