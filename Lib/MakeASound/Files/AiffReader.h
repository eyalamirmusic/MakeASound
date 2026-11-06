#pragma once

/** @file AiffReader, which decodes the samples of an AIFF or AIFF-C file from a ByteSource. */

#include "ByteStreams.h"

#include <cstdint>
#include <vector>

namespace MakeASound::Files
{
/// Reads an AIFF or AIFF-C file from a ByteSource: big-endian PCM 8, 16, 24 and 32
/// bits (AIFF, and AIFF-C 'NONE'/'twos'), little-endian PCM ('sowt'), and
/// big-endian IEEE float 32 and 64 bits ('fl32'/'FL32', 'fl64'/'FL64'). Samples
/// only: no metadata is read. A file cut short reads as far as its bytes go.
///
/// The source is not owned and must outlive the reader (or the next open()).
/// Not thread-safe: one thread reads at a time.
class AiffReader
{
public:
    AiffReader() = default;

    AiffReader(const AiffReader&) = delete;
    AiffReader& operator=(const AiffReader&) = delete;

    /// True when the file is an AIFF or AIFF-C this reader can decode; false (and
    /// closed, every accessor at its default) otherwise.
    bool open(ByteSource& source);
    bool isOpen() const noexcept;

    int numChannels() const noexcept;
    double sampleRate() const noexcept; ///< the 80-bit rate, exactly as a double
    int bitDepth() const noexcept; ///< 8, 16, 24, 32 or 64
    bool isFloat() const noexcept;
    std::int64_t numFrames() const noexcept;

    /// As WavReader's: frames outside the file read as 0, a null channel pointer
    /// is skipped, false when closed, numFramesToRead < 0 or dest is null. Never
    /// allocates.
    bool read(std::int64_t startFrame,
              int numFramesToRead,
              float* const* dest) noexcept;
    bool readInt32(std::int64_t startFrame,
                   int numFramesToRead,
                   std::int32_t* const* dest) noexcept;

private:
    /// Back to the closed state, every accessor at its default.
    void forget() noexcept;

    ByteSource* source = nullptr;
    int channels = 0;
    double rate = 0.0;
    int bits = 0;
    bool floatingPoint = false;
    bool littleEndian = false;
    std::int64_t frames = 0;
    std::int64_t dataOffset = 0;
    std::vector<std::uint8_t> scratch;
};
} // namespace MakeASound::Files
