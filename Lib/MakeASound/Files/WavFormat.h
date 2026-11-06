#pragma once

// Internal: the WAV format-chunk constants WavReader and WavWriter share.

#include <cstdint>

namespace MakeASound::Files::Detail
{
constexpr std::uint16_t wavTagPcm = 1;
constexpr std::uint16_t wavTagFloat = 3;
constexpr std::uint16_t wavTagExtensible = 0xfffe;

// A RIFF or data size field that holds no size: RF64 keeps the real one in ds64,
// and in a RIFF file it marks a writer that never came back to fill it in.
constexpr std::uint32_t sizeUnknown = 0xffffffffu;

// An extensible format chunk's 16-byte sub-format GUID is the plain format tag
// (a little-endian u32: 1 PCM, 3 float) followed by one of these 12 bytes.
inline constexpr std::uint8_t standardSubFormatTail[12] = {
    0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};

// Ambisonic B-format, read as the same PCM or float samples.
inline constexpr std::uint8_t ambisonicSubFormatTail[12] = {
    0x21, 0x07, 0xd3, 0x11, 0x86, 0x44, 0xc8, 0xc1, 0xca, 0x00, 0x00, 0x00};
} // namespace MakeASound::Files::Detail
