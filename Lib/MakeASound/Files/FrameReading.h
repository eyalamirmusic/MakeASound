#pragma once

/** @file
    Internal: what WavReader and AiffReader share: reading chunk payloads while
    parsing, and decoding interleaved frames from a ByteSource into per-channel
    float or full-scale int32 buffers.
*/

#include "ByteStreams.h"

#include <cstdint>
#include <vector>

namespace MakeASound::Files::Detail
{
/// Up to 'count' bytes from 'offset', fewer if the source ends first.
std::vector<std::uint8_t>
    readChunkBytes(ByteSource& source, std::int64_t offset, std::int64_t count);

/// Where a chunk walk stops: 8 + the form size, but never past the source, and the
/// whole source for a size of 0 (a writer that never filled it in).
std::int64_t walkLimit(std::uint64_t formSize, std::int64_t sourceSize) noexcept;

/// How one sample is stored in the file: width, signedness, integer or float, byte order.
enum class Encoding
{
    unsigned8, ///< WAV 8-bit: offset binary
    signed8,
    int16LE,
    int16BE,
    int24LE,
    int24BE,
    int32LE,
    int32BE,
    float32LE,
    float32BE,
    float64LE,
    float64BE
};

int bytesPerSample(Encoding encoding) noexcept;

/// An open file's samples: numFrames interleaved frames of numChannels samples,
/// starting at dataOffset in the source.
struct FrameLayout
{
    ByteSource* source = nullptr;
    std::int64_t dataOffset = 0;
    std::int64_t numFrames = 0;
    int numChannels = 0;
    Encoding encoding = Encoding::int16LE;
};

/// The buffer readFrames decodes through: a whole number of frames, at least one.
/// Allocated once, when a reader opens.
std::vector<std::uint8_t> makeReadScratch(int numChannels, Encoding encoding);

/// Frames startFrame .. startFrame + numFramesToRead - 1 into dest[c] for every
/// channel whose pointer is not null; frames outside the file, or that the source
/// no longer supplies, read as 0. Never allocates.
void readFrames(const FrameLayout& layout,
                Span<std::uint8_t> scratch,
                std::int64_t startFrame,
                int numFramesToRead,
                float* const* dest) noexcept;

void readFrames(const FrameLayout& layout,
                Span<std::uint8_t> scratch,
                std::int64_t startFrame,
                int numFramesToRead,
                std::int32_t* const* dest) noexcept;
} // namespace MakeASound::Files::Detail
