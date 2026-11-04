#pragma once

#include "ByteStreams.h"

#include <cstdio>
#include <filesystem>

namespace MakeASound::Files
{
// A ByteSource over a file opened for reading, with 64-bit offsets on every
// platform. The file stays open until the source is destroyed.
class FileSource : public ByteSource
{
public:
    explicit FileSource(const std::filesystem::path& path);
    ~FileSource() override;

    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    // False when the file could not be opened; size() is then 0 and read() 0.
    bool isOpen() const noexcept;

    std::int64_t size() const noexcept override;
    int read(std::int64_t offset, Span<std::uint8_t> dest) noexcept override;

private:
    std::FILE* file = nullptr;
    std::int64_t fileSize = 0;
};

// A ByteSink that creates (or truncates) a file and writes to it, with 64-bit
// offsets on every platform. The file is closed when the sink is destroyed or
// close() is called.
class FileSink : public ByteSink
{
public:
    explicit FileSink(const std::filesystem::path& path);
    ~FileSink() override;

    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;

    // False when the file could not be created; every write then fails.
    bool isOpen() const noexcept;

    bool write(Span<const std::uint8_t> bytes) override;
    bool seek(std::int64_t newPosition) override;
    std::int64_t position() const noexcept override;

    // Flushes and closes the file. False if the flush or the close failed, or the
    // file was not open. Writes after it fail.
    bool close();

private:
    std::FILE* file = nullptr;
    std::int64_t writePosition = 0;
    std::int64_t written = 0;
};
} // namespace MakeASound::Files
