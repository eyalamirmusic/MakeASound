#include "MemoryStreams.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace MakeASound::Files
{
MemorySource::MemorySource(std::vector<std::uint8_t> bytesToOwn)
    : owned(std::move(bytesToOwn))
    , view(owned)
{
}

MemorySource::MemorySource(Span<const std::uint8_t> bytesToView) noexcept
    : view(bytesToView)
{
}

std::int64_t MemorySource::size() const noexcept
{
    return (std::int64_t) view.getSize();
}

int MemorySource::read(std::int64_t offset, Span<std::uint8_t> dest) noexcept
{
    auto total = size();

    if (offset < 0 || offset >= total || dest.empty())
        return 0;

    auto count = std::min((std::int64_t) dest.getSize(), total - offset);
    std::memcpy(dest.data(), view.data() + offset, (std::size_t) count);
    return (int) count;
}

bool MemorySink::write(Span<const std::uint8_t> bytes)
{
    if (bytes.empty())
        return true;

    auto count = bytes.getSize();
    auto at = (std::size_t) writePosition;
    auto overlap = std::min(count, data.size() - at);

    if (overlap > 0)
        std::memcpy(data.data() + at, bytes.data(), overlap);

    data.insert(data.end(), bytes.begin() + overlap, bytes.end());
    writePosition += (std::int64_t) count;
    return true;
}

bool MemorySink::seek(std::int64_t newPosition)
{
    if (newPosition < 0 || newPosition > (std::int64_t) data.size())
        return false;

    writePosition = newPosition;
    return true;
}

std::int64_t MemorySink::position() const noexcept
{
    return writePosition;
}

const std::vector<std::uint8_t>& MemorySink::getData() const noexcept
{
    return data;
}

std::vector<std::uint8_t> MemorySink::takeData() noexcept
{
    auto taken = std::move(data);
    data.clear();
    writePosition = 0;
    return taken;
}
} // namespace MakeASound::Files
