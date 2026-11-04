#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace MakeASound::Files
{
// A four-character code (a RIFF or AIFF chunk id, a LIST type, an INFO key), held
// as its four bytes in file order. Built from a literal: FourCC id = "data";
struct FourCC
{
    constexpr FourCC() noexcept = default;

    constexpr FourCC(const char (&text)[5]) noexcept
        : bytes {(std::uint8_t) text[0],
                 (std::uint8_t) text[1],
                 (std::uint8_t) text[2],
                 (std::uint8_t) text[3]}
    {
    }

    // The four bytes read as a little-endian word, the way RIFF stores numbers:
    // FourCC("data").asLittleEndian() == 0x61746164.
    constexpr std::uint32_t asLittleEndian() const noexcept
    {
        return (std::uint32_t) bytes[0] | ((std::uint32_t) bytes[1] << 8)
               | ((std::uint32_t) bytes[2] << 16) | ((std::uint32_t) bytes[3] << 24);
    }

    static constexpr FourCC fromLittleEndian(std::uint32_t word) noexcept
    {
        auto code = FourCC {};
        code.bytes = {(std::uint8_t) word,
                      (std::uint8_t) (word >> 8),
                      (std::uint8_t) (word >> 16),
                      (std::uint8_t) (word >> 24)};
        return code;
    }

    // The four bytes as a string of length 4 (bytes as they are, zeros included).
    std::string toString() const
    {
        return std::string(bytes.begin(), bytes.end());
    }

    constexpr bool operator==(const FourCC&) const noexcept = default;

    std::array<std::uint8_t, 4> bytes {};
};
} // namespace MakeASound::Files
