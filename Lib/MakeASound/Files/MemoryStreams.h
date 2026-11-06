#pragma once

#include "ByteStreams.h"

#include <vector>

namespace MakeASound::Files
{
// A ByteSource over bytes in memory: either a vector it owns, or a view of bytes
// someone else keeps alive for as long as the source is used (a memory-mapped
// file, an embedded binary).
class MemorySource : public ByteSource
{
public:
    MemorySource() = default;

    // Takes the bytes.
    explicit MemorySource(std::vector<std::uint8_t> bytesToOwn);

    // Views the bytes without copying them.
    explicit MemorySource(Span<const std::uint8_t> bytesToView) noexcept;

    MemorySource(const MemorySource&) = delete;
    MemorySource& operator=(const MemorySource&) = delete;

    std::int64_t size() const noexcept override;
    int read(std::int64_t offset, Span<std::uint8_t> dest) noexcept override;

private:
    std::vector<std::uint8_t> owned;
    Span<const std::uint8_t> view;
};

// A ByteSink that grows a vector it owns.
class MemorySink : public ByteSink
{
public:
    MemorySink() = default;

    bool write(Span<const std::uint8_t> bytes) override;
    bool seek(std::int64_t newPosition) override;
    std::int64_t position() const noexcept override;

    // Everything written so far, of length the furthest position ever reached.
    const std::vector<std::uint8_t>& getData() const noexcept;

    // Moves the bytes out; the sink is then empty, at position 0.
    std::vector<std::uint8_t> takeData() noexcept;

private:
    std::vector<std::uint8_t> data;
    std::int64_t writePosition = 0;
};
} // namespace MakeASound::Files
