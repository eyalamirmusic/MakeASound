#include "FileStreams.h"
#include "FileAccess.h"

#include <algorithm>

namespace MakeASound::Files
{
FileSource::FileSource(const std::filesystem::path& path)
    : file(Detail::openFile(path, false))
{
    if (file == nullptr)
        return;

    // Unbuffered: stdio would otherwise allocate its buffer on the first read,
    // and read() promises not to allocate. The readers read in large blocks.
    std::setvbuf(file, nullptr, _IONBF, 0);

    auto length = Detail::seekFile(file, 0, SEEK_END) ? Detail::tellFile(file) : -1;

    if (length < 0)
    {
        std::fclose(file);
        file = nullptr;
        return;
    }

    fileSize = length;
}

FileSource::~FileSource()
{
    if (file != nullptr)
        std::fclose(file);
}

bool FileSource::isOpen() const noexcept
{
    return file != nullptr;
}

std::int64_t FileSource::size() const noexcept
{
    return fileSize;
}

int FileSource::read(std::int64_t offset, Span<std::uint8_t> dest) noexcept
{
    if (file == nullptr || offset < 0 || offset >= fileSize || dest.empty())
        return 0;

    auto count = std::min((std::int64_t) dest.getSize(), fileSize - offset);

    if (!Detail::seekFile(file, offset, SEEK_SET))
        return 0;

    return (int) std::fread(dest.data(), 1, (std::size_t) count, file);
}

FileSink::FileSink(const std::filesystem::path& path)
    : file(Detail::openFile(path, true))
{
}

FileSink::~FileSink()
{
    if (file != nullptr)
        std::fclose(file);
}

bool FileSink::isOpen() const noexcept
{
    return file != nullptr;
}

bool FileSink::write(Span<const std::uint8_t> bytes)
{
    if (file == nullptr)
        return false;

    if (bytes.empty())
        return true;

    auto count = std::fwrite(bytes.data(), 1, bytes.getSize(), file);
    writePosition += (std::int64_t) count;
    written = std::max(written, writePosition);
    return count == bytes.getSize();
}

bool FileSink::seek(std::int64_t newPosition)
{
    if (file == nullptr || newPosition < 0 || newPosition > written)
        return false;

    if (!Detail::seekFile(file, newPosition, SEEK_SET))
        return false;

    writePosition = newPosition;
    return true;
}

std::int64_t FileSink::position() const noexcept
{
    return writePosition;
}

bool FileSink::close()
{
    if (file == nullptr)
        return false;

    auto flushed = std::fflush(file) == 0;
    auto closed = std::fclose(file) == 0;
    file = nullptr;
    return flushed && closed;
}
} // namespace MakeASound::Files
