#pragma once

#include "ByteStreams.h"
#include "Metadata.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace MakeASound::Files
{
enum class BitDepth
{
    pcm8, // unsigned
    pcm16,
    pcm24,
    pcm32,
    float32
};

struct WavWriterOptions
{
    double sampleRate = 44100.0; // stored as its integer part
    int numChannels = 2;
    BitDepth bitDepth = BitDepth::pcm24;

    // Written in order, before the data or after it (metadataBeforeData).
    Metadata metadata;

    // A 'JUNK' chunk after 'WAVE' sized so the header can become an RF64 one in
    // place: the first chunk after the format chunk then always starts at byte 96
    // (from the header's start), whatever the format chunk's form. Without it a
    // file cannot grow past what a RIFF size holds.
    bool headerPadding = true;

    // The metadata chunks go between the format chunk and 'data' (true), or after
    // the data, written by finish() (false).
    bool metadataBeforeData = true;

    // The extensible format chunk's speaker mask. Unset: 0x7, 0x33, 0x37, 0x3f,
    // 0xf7 and 0xff for 3 to 8 channels, and none (so the plain format chunk) for
    // 1, 2 or more than 8. A mask of 0 also means the plain format chunk.
    std::optional<std::uint32_t> channelMask;

    // Writes the RF64 form (RF64, ds64, extensible format chunk) from the start,
    // whatever the size. Otherwise a file becomes RF64 only once its sizes no
    // longer fit 32 bits.
    bool alwaysRF64 = false;
};

// Writes a WAV file into a ByteSink: the header at open(), samples as they come,
// and the final sizes, patched into the header, at finish(). The header starts
// at the sink's position at open().
//
// The sink is not owned and must outlive the writer, or its finish().
class WavWriter
{
public:
    WavWriter() = default;

    // Finishes the file if open() succeeded and finish() was not called. Its result
    // (and any exception from the sink) is lost there: call finish() to know.
    ~WavWriter();

    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    // Validates the options and writes the header of a file with no frames. False
    // (and nothing written) for invalid options or a sink that refuses the bytes.
    bool open(ByteSink& sink, const WavWriterOptions& options);
    bool isOpen() const noexcept;

    // Appends numFramesToWrite frames, channel c from channels[c] (a null channel
    // writes silence). Float to integer through floatToInt32 then an arithmetic
    // shift to the depth; float32 files store the floats' bits unchanged.
    bool write(const float* const* channels, int numFramesToWrite);

    // The same from full-scale int32 samples: shifted to the depth, or for a
    // float32 file int32ToFloat.
    bool writeInt32(const std::int32_t* const* channels, int numFramesToWrite);

    // Pads the data to an even length, writes the metadata if it goes after the
    // data, rewrites the header with the final sizes (as RF64 if they need it) and
    // leaves the sink's position at the end of the file. The writer is closed
    // afterwards. False if it was not open or a sink operation failed.
    //
    // True means the sink accepted every byte, not that they reached a disk: a
    // FileSink buffers, so a full disk can show only when it flushes. Check
    // FileSink::close() as well before trusting the file.
    bool finish();

    std::int64_t framesWritten() const noexcept;

private:
    template <typename Sample>
    bool appendFrames(const Sample* const* channels, int numFramesToWrite);

    // Everything before the samples, for these sizes: the RIFF or RF64 header, the
    // format chunk, the metadata if it goes first, and the data chunk's header.
    std::vector<std::uint8_t> buildHeader(std::uint64_t riffSize, bool asRF64) const;

    ByteSink* sink = nullptr;
    BitDepth depth = BitDepth::pcm24;
    int numChannels = 0;
    std::uint32_t storedRate = 0;
    std::uint32_t mask = 0;
    bool padded = true;
    bool metadataFirst = true;
    bool alwaysRF64 = false;
    std::vector<std::uint8_t> metadataBytes;
    std::int64_t headerStart = 0;
    std::int64_t headerBytes = 0;
    std::int64_t dataBytes = 0;
    std::int64_t frames = 0;
    bool failed = false;
    std::vector<std::uint8_t> scratch;
};
} // namespace MakeASound::Files
