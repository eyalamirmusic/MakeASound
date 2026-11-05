#pragma once

#include "../Common/Common.h"

#include <cstdint>

namespace MakeASound::Files
{
// Random-access bytes to read an audio file from: a file, a block of memory, a
// range of a memory-mapped file, a consumer's own stream. The readers never
// assume more than this.
class ByteSource
{
public:
    virtual ~ByteSource() = default;

    // The number of bytes the source holds; offsets run from 0 to size().
    virtual std::int64_t size() const noexcept = 0;

    // Copies up to dest.size() bytes starting at 'offset' into dest and returns
    // how many it copied: fewer than asked at the end of the source, 0 at or past
    // it, for a negative offset, or on an error. Never allocates.
    virtual int read(std::int64_t offset, Span<std::uint8_t> dest) noexcept = 0;
};

// Where a writer puts its bytes: a sequential write position that can be moved
// back, so a header written first can be patched once the sizes are known.
class ByteSink
{
public:
    virtual ~ByteSink() = default;

    // Writes every byte at the current position, overwriting what is there and
    // extending the sink past its end, and advances the position by bytes.size().
    // False if not every byte could be written.
    virtual bool write(Span<const std::uint8_t> bytes) = 0;

    // Moves the write position to an absolute offset between 0 and the number of
    // bytes written so far (inclusive). False, with the position unchanged, for
    // any other offset or when the sink cannot seek.
    virtual bool seek(std::int64_t newPosition) = 0;

    // The current write position.
    virtual std::int64_t position() const noexcept = 0;
};
} // namespace MakeASound::Files
