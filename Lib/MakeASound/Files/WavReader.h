#pragma once

#include "ByteStreams.h"
#include "Metadata.h"

#include <cstdint>
#include <vector>

namespace MakeASound::Files
{
// Reads a WAV file (RIFF or RF64) from a ByteSource: PCM 8 (unsigned), 16, 24 and
// 32 bits, IEEE float 32 and 64 bits, in the plain or the WAVE_FORMAT_EXTENSIBLE
// format chunk, with its chunks in any order, and its metadata chunks as a
// Metadata. A file cut short reads as far as its bytes go.
//
// The source is not owned and must outlive the reader (or the next open()).
// Not thread-safe: one thread reads at a time.
class WavReader
{
public:
    WavReader() = default;

    WavReader(const WavReader&) = delete;
    WavReader& operator=(const WavReader&) = delete;

    // Parses the header and every chunk. True when the file is a WAV this reader
    // can decode; false (and closed, every accessor at its default) otherwise.
    // Opening again forgets the previous file.
    bool open(ByteSource& source);
    bool isOpen() const noexcept;

    int numChannels() const noexcept; // 0 when closed
    double sampleRate() const noexcept; // 0 when closed
    int bitDepth() const noexcept; // 8, 16, 24, 32 or 64; 0 when closed
    bool isFloat() const noexcept;
    std::int64_t numFrames() const noexcept; // 0 when closed
    bool isRF64() const noexcept;

    // The extensible format chunk's speaker mask, 0 for the plain format chunk.
    std::uint32_t channelMask() const noexcept;

    const Metadata& metadata() const noexcept;

    // Reads numFramesToRead frames from frame 'startFrame' into dest[0] to
    // dest[numChannels() - 1], each as float (see SampleConversion.h). Frames
    // before 0 or past the end read as 0; a null channel pointer is skipped. False
    // (dest untouched) when closed, numFramesToRead < 0 or dest is null. Never
    // allocates.
    bool read(std::int64_t startFrame,
              int numFramesToRead,
              float* const* dest) noexcept;

    // The same, each sample as a full-scale int32 (left-justified integers; float
    // samples through floatToInt32).
    bool readInt32(std::int64_t startFrame,
                   int numFramesToRead,
                   std::int32_t* const* dest) noexcept;

private:
    // Back to the closed state, every accessor at its default.
    void forget() noexcept;

    ByteSource* source = nullptr;
    int channels = 0;
    double rate = 0.0;
    int bits = 0;
    bool floatingPoint = false;
    bool rf64 = false;
    std::uint32_t mask = 0;
    std::int64_t frames = 0;
    std::int64_t dataOffset = 0;
    Metadata chunks;
    std::vector<std::uint8_t> scratch;
};
} // namespace MakeASound::Files
