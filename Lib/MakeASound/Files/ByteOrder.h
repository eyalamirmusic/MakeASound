#pragma once

/** @file
    Internal: little- and big-endian loads and stores on raw bytes, and a view of a
    chunk payload that reads 0 past the bytes present. Shared by the readers and the
    writer; not part of the public interface.
*/

#include <cstdint>
#include <string>

namespace MakeASound::Files::Detail
{
inline std::uint16_t loadU16LE(const std::uint8_t* bytes) noexcept
{
    return (std::uint16_t) (bytes[0] | (bytes[1] << 8));
}

inline std::uint32_t loadU32LE(const std::uint8_t* bytes) noexcept
{
    return (std::uint32_t) bytes[0] | ((std::uint32_t) bytes[1] << 8)
           | ((std::uint32_t) bytes[2] << 16) | ((std::uint32_t) bytes[3] << 24);
}

inline std::uint64_t loadU64LE(const std::uint8_t* bytes) noexcept
{
    return (std::uint64_t) loadU32LE(bytes)
           | ((std::uint64_t) loadU32LE(bytes + 4) << 32);
}

inline std::uint16_t loadU16BE(const std::uint8_t* bytes) noexcept
{
    return (std::uint16_t) ((bytes[0] << 8) | bytes[1]);
}

inline std::uint32_t loadU32BE(const std::uint8_t* bytes) noexcept
{
    return ((std::uint32_t) bytes[0] << 24) | ((std::uint32_t) bytes[1] << 16)
           | ((std::uint32_t) bytes[2] << 8) | (std::uint32_t) bytes[3];
}

inline std::uint64_t loadU64BE(const std::uint8_t* bytes) noexcept
{
    return ((std::uint64_t) loadU32BE(bytes) << 32)
           | (std::uint64_t) loadU32BE(bytes + 4);
}

inline void storeU16LE(std::uint8_t* bytes, std::uint32_t number) noexcept
{
    bytes[0] = (std::uint8_t) number;
    bytes[1] = (std::uint8_t) (number >> 8);
}

inline void storeU32LE(std::uint8_t* bytes, std::uint32_t number) noexcept
{
    bytes[0] = (std::uint8_t) number;
    bytes[1] = (std::uint8_t) (number >> 8);
    bytes[2] = (std::uint8_t) (number >> 16);
    bytes[3] = (std::uint8_t) (number >> 24);
}

/// The bytes of a chunk payload of which 'size' are present. Every load past them
/// (or before 0) reads 0, so a chunk cut short parses with its missing fields 0.
struct PresentBytes
{
    std::uint8_t at(std::int64_t offset) const noexcept
    {
        return offset >= 0 && offset < size ? bytes[offset] : 0;
    }

    std::uint16_t u16(std::int64_t offset) const noexcept
    {
        return (std::uint16_t) (at(offset) | (at(offset + 1) << 8));
    }

    std::uint32_t u32(std::int64_t offset) const noexcept
    {
        return (std::uint32_t) u16(offset) | ((std::uint32_t) u16(offset + 2) << 16);
    }

    std::uint64_t u64(std::int64_t offset) const noexcept
    {
        return (std::uint64_t) u32(offset) | ((std::uint64_t) u32(offset + 4) << 32);
    }

    std::uint32_t u32BE(std::int64_t offset) const noexcept
    {
        return ((std::uint32_t) at(offset) << 24)
               | ((std::uint32_t) at(offset + 1) << 16)
               | ((std::uint32_t) at(offset + 2) << 8)
               | (std::uint32_t) at(offset + 3);
    }

    std::uint16_t u16BE(std::int64_t offset) const noexcept
    {
        return (std::uint16_t) ((at(offset) << 8) | at(offset + 1));
    }

    /// The bytes from 'offset' to the first zero, to offset + width, or to the end
    /// of what is present, whichever comes first.
    std::string text(std::int64_t offset, std::int64_t width) const
    {
        auto out = std::string();

        for (auto index = offset; index < offset + width && index < size; ++index)
        {
            if (index < 0 || bytes[index] == 0)
                break;

            out.push_back((char) bytes[index]);
        }

        return out;
    }

    /// The present bytes in [offset, offset + length), clipped to what is there.
    PresentBytes range(std::int64_t offset, std::int64_t length) const noexcept
    {
        if (offset < 0 || offset >= size || length <= 0)
            return {};

        auto available = size - offset;
        return {bytes + offset, length < available ? length : available};
    }

    const std::uint8_t* bytes = nullptr;
    std::int64_t size = 0;
};
} // namespace MakeASound::Files::Detail
