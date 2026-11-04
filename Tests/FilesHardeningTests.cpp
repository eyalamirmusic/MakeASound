// Tests for what a review of MakeASoundFiles asked to pin: a source that shrinks
// after open(), no allocation while reading, reads across the decode buffer's
// boundaries, the unpadded size limit, file offsets past 2^31, hostile headers,
// the bounds on what open() holds, and finish() failing late.

#include "FilesTestHelpers.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace FilesHardeningTests
{
using namespace nano;
using namespace FilesTestHelpers;

using Files::AiffReader;
using Files::BitDepth;
using Files::MemorySink;
using Files::MemorySource;
using Files::WavReader;
using Files::WavWriter;
using Files::WavWriterOptions;

namespace
{
using ByteSpan = MakeASound::Span<std::uint8_t>;

// Bytes that can be cut short after a reader has opened them. A length set past
// the bytes makes a source whose size() promises more than read() delivers.
class ShrinkingSource : public Files::ByteSource
{
public:
    explicit ShrinkingSource(Bytes bytesToUse)
        : bytes(std::move(bytesToUse))
        , length((std::int64_t) bytes.size())
    {
    }

    std::int64_t size() const noexcept override { return length; }

    int read(std::int64_t offset, ByteSpan dest) noexcept override
    {
        auto held = std::min(length, (std::int64_t) bytes.size());

        if (offset < 0 || offset >= held || dest.empty())
            return 0;

        auto count = std::min((std::int64_t) dest.getSize(), held - offset);
        std::memcpy(dest.data(), bytes.data() + offset, (std::size_t) count);
        return (int) count;
    }

    Bytes bytes;
    std::int64_t length;
};

// A source of 'total' bytes holding a few runs of real bytes and zeros everywhere
// else: a file of gigabytes that costs nothing to build.
class SparseSource : public Files::ByteSource
{
public:
    struct Run
    {
        std::int64_t offset;
        Bytes bytes;
    };

    std::int64_t size() const noexcept override { return total; }

    int read(std::int64_t offset, ByteSpan dest) noexcept override
    {
        if (offset < 0 || offset >= total || dest.empty())
            return 0;

        auto count = std::min((std::int64_t) dest.getSize(), total - offset);
        std::fill(dest.data(), dest.data() + count, std::uint8_t {0});

        for (auto& run: runs)
        {
            auto from = std::max(offset, run.offset);
            auto to = std::min(offset + count,
                               run.offset + (std::int64_t) run.bytes.size());

            for (auto at = from; at < to; ++at)
                dest.data()[at - offset] =
                    run.bytes[(std::size_t) (at - run.offset)];
        }

        return (int) count;
    }

    std::vector<Run> runs;
    std::int64_t total = 0;
};

// A MemorySink that refuses any write reaching past 'limit' bytes.
class LimitedSink : public MemorySink
{
public:
    explicit LimitedSink(std::int64_t limitToUse)
        : limit(limitToUse)
    {
    }

    bool write(MakeASound::Span<const std::uint8_t> bytes) override
    {
        if (position() + (std::int64_t) bytes.getSize() > limit)
            return false;

        return MemorySink::write(bytes);
    }

    std::int64_t limit;
};

// A MemorySink that throws from write() once armed, as a consumer's sink might.
class ThrowingSink : public MemorySink
{
public:
    bool write(MakeASound::Span<const std::uint8_t> bytes) override
    {
        if (armed)
            throw std::runtime_error("sink failure");

        return MemorySink::write(bytes);
    }

    bool armed = false;
};

struct OpenedWav
{
    explicit OpenedWav(const Bytes& bytes)
        : source(Bytes(bytes))
    {
        ok = reader.open(source);
    }

    MemorySource source;
    WavReader reader;
    bool ok = false;
};

struct OpenedAiff
{
    explicit OpenedAiff(const Bytes& bytes)
        : source(Bytes(bytes))
    {
        ok = reader.open(source);
    }

    MemorySource source;
    AiffReader reader;
    bool ok = false;
};

Bytes patterned(int count)
{
    auto bytes = Bytes((std::size_t) count);

    for (auto i = 0; i < count; ++i)
        bytes[(std::size_t) i] = (std::uint8_t) (i * 37 + 11);

    return bytes;
}

Bytes plainWav(int tag, int channels, int bits, const Bytes& samples)
{
    return riffFile({riffChunk("fmt ", plainFormat(tag, channels, 44100, bits)),
                     riffChunk("data", samples)});
}

// COMM for 44.1 kHz, with an AIFF-C compression type when one is given.
Bytes aiffCommon(std::uint32_t channelsField,
                 std::uint32_t frames,
                 int sampleSize,
                 std::string_view compression = {})
{
    auto out = ByteWriter {};
    out.u16be(channelsField).u32be(frames).u16be((std::uint32_t) sampleSize);
    out.u8(0x40).u8(0x0e).u8(0xac).u8(0x44).zeros(6);

    if (!compression.empty())
        out.text(compression).zeros(2);

    return aiffChunk("COMM", out.bytes);
}

Bytes aiffSound(const Bytes& samples, std::uint32_t offsetField = 0)
{
    auto out = ByteWriter {};
    out.u32be(offsetField).u32be(0).append(samples);
    return aiffChunk("SSND", out.bytes);
}

// --- A source that shrinks ----------------------------------------------------

auto tShrinking = test("Files/Hardening/aSourceThatShrinksReadsZeros") = []
{
    auto samples = ByteWriter {};

    for (auto s = 1; s <= 10; ++s)
        samples.u16((std::uint32_t) s * 1024);

    // The data starts at 44: RIFF header 12, fmt 24, data header 8.
    auto source = ShrinkingSource(plainWav(1, 1, 16, samples.bytes));
    auto reader = WavReader();
    check(reader.open(source));
    check(reader.numFrames() == 10);

    source.length = 44 + 4 * 2 + 1; // four frames and one byte of the fifth
    auto out = std::vector<std::int32_t>(12, 7);
    std::int32_t* dest[] = {out.data()};
    check(reader.readInt32(-1, 12, dest));
    check(out
          == (std::vector<std::int32_t> {0,
                                         1024 << 16,
                                         2048 << 16,
                                         3072 << 16,
                                         4096 << 16,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0}));

    // The count stays what open() found; what the source no longer has reads 0.
    check(reader.numFrames() == 10);

    source.length = 0;
    out.assign(12, 7);
    check(reader.readInt32(0, 12, dest));
    check(std::all_of(out.begin(), out.end(), [](auto v) { return v == 0; }));

    auto big = ByteWriter {};
    big.u16be(0x4000).u16be(0x2000).u16be(0x1000);
    auto aiff = ShrinkingSource(
        aiffFile("AIFF", {aiffCommon(1, 3, 16), aiffSound(big.bytes)}));
    auto aiffReader = AiffReader();
    check(aiffReader.open(aiff));
    check(aiffReader.numFrames() == 3);

    // The samples start at 54: FORM header 12, COMM 26, SSND header 8 + 8.
    aiff.length = 54 + 2;
    auto floats = std::vector<float>(4, 9.f);
    float* floatDest[] = {floats.data()};
    check(aiffReader.read(0, 4, floatDest));
    check(floats == (std::vector<float> {0.5f, 0.f, 0.f, 0.f}));
};

// --- No allocation while reading ----------------------------------------------

template <typename Reader>
bool readsWithoutAllocating(Reader& reader)
{
    auto floats = std::vector<std::vector<float>>((std::size_t) reader.numChannels(),
                                                  std::vector<float>(300));
    auto ints = std::vector<std::vector<std::int32_t>>(
        (std::size_t) reader.numChannels(), std::vector<std::int32_t>(300));
    auto floatDest = std::vector<float*>();
    auto intDest = std::vector<std::int32_t*>();

    for (auto& channel: floats)
        floatDest.push_back(channel.data());

    for (auto& channel: ints)
        intDest.push_back(channel.data());

    auto stats = allocationsIn(
        [&]
        {
            reader.read(-5, 300, floatDest.data());
            reader.readInt32(3, 300, intDest.data());
            reader.read(100000, 10, floatDest.data());
            reader.readInt32(0, 0, intDest.data());
        });

    return stats.count == 0;
}

auto tNoAllocation = test("Files/Hardening/readsNeverAllocate") = []
{
    // The watch sees an allocation at all (called through pointers the optimiser
    // cannot see through, so the pair is not elided).
    volatile auto allocate = static_cast<void* (*)(std::size_t)>(::operator new);
    volatile auto release = static_cast<void (*)(void*)>(::operator delete);
    auto* block = static_cast<void*>(nullptr);
    auto seen = allocationsIn([&] { block = allocate(64); });
    release(block);
    check(seen.count == 1 && seen.largest == 64);

    struct Case
    {
        int tag, channels, bits;
    };

    for (auto c: {Case {1, 2, 8},
                  Case {1, 2, 16},
                  Case {1, 3, 24},
                  Case {1, 1, 32},
                  Case {3, 2, 32},
                  Case {3, 2, 64}})
    {
        auto file = OpenedWav(plainWav(
            c.tag, c.channels, c.bits, patterned(c.channels * c.bits / 8 * 1000)));
        check(file.ok);
        check(readsWithoutAllocating(file.reader), "WAV from memory");
    }

    auto extensible = OpenedWav(
        riffFile({riffChunk("fmt ", extensibleFormat(3, 48000, 24, 0x7, pcmGuid)),
                  riffChunk("data", patterned(3 * 3 * 500))}));
    check(extensible.ok && readsWithoutAllocating(extensible.reader));

    for (auto codec: {"NONE", "sowt", "fl64"})
    {
        auto bits = std::string_view(codec) == "fl64" ? 64 : 24;
        auto file =
            OpenedAiff(aiffFile("AIFC",
                                {aiffCommon(2, 1000, bits, codec),
                                 aiffSound(patterned(2 * bits / 8 * 1000))}));
        check(file.ok);
        check(readsWithoutAllocating(file.reader), codec);
    }

    // And from a real file, through FileSource.
    auto dir = std::filesystem::temp_directory_path() / "MakeASoundFilesTests";
    std::filesystem::create_directories(dir);
    auto path = dir / "hardening-no-allocation.wav";

    {
        auto sink = Files::FileSink(path);
        auto writer = WavWriter();
        auto options = WavWriterOptions {};
        options.bitDepth = BitDepth::pcm16;
        check(writer.open(sink, options));
        auto input = synthetic(2, 1000, 4);
        auto pointers = pointersTo(input);
        check(writer.write(pointers.data(), 1000));
        check(writer.finish());
        check(sink.close());
    }

    {
        auto source = Files::FileSource(path);
        auto reader = WavReader();
        check(reader.open(source));
        check(readsWithoutAllocating(reader), "WAV from a file");
    }

    std::filesystem::remove(path);
};

// --- Reads across the decode buffer's boundaries --------------------------------

auto tBoundaries = test("Files/Hardening/readsAcrossTheDecodeBuffer") = []
{
    // The readers decode through a 64 KiB buffer: 16384 stereo 16-bit frames,
    // 10922 stereo 24-bit ones, 4096 stereo float64 ones.
    constexpr auto frames = 50000; // past three buffers of either depth

    struct Case
    {
        BitDepth depth;
        std::uint32_t keep;
        int bufferFrames;
    };

    for (auto c: {Case {BitDepth::pcm16, 0xffff0000u, 16384},
                  Case {BitDepth::pcm24, 0xffffff00u, 10922}})
    {
        auto values = std::vector<std::vector<std::int32_t>>(
            2, std::vector<std::int32_t>(frames));

        for (auto channel = 0; channel < 2; ++channel)
            for (auto i = 0; i < frames; ++i)
                values[(std::size_t) channel][(std::size_t) i] =
                    (std::int32_t) (((std::uint32_t) i * 2654435761u
                                     + (std::uint32_t) channel * 40503u)
                                    & c.keep);

        auto sink = MemorySink();
        auto writer = WavWriter();
        auto options = WavWriterOptions {};
        options.bitDepth = c.depth;
        check(writer.open(sink, options));
        const std::int32_t* input[] = {values[0].data(), values[1].data()};
        check(writer.writeInt32(input, frames));
        check(writer.finish());

        auto file = OpenedWav(sink.getData());
        check(file.ok && file.reader.numFrames() == frames);
        check(readAllInt32(file.reader) == values);

        auto left = std::vector<std::int32_t>(30001);
        auto right = std::vector<std::int32_t>(30001);
        std::int32_t* dest[] = {left.data(), right.data()};

        for (auto k = 1; k <= 3; ++k)
        {
            auto start = k * c.bufferFrames - 3;
            check(file.reader.readInt32(start, 7, dest));

            for (auto i = 0; i < 7; ++i)
                check(left[(std::size_t) i] == values[0][(std::size_t) (start + i)]
                      && right[(std::size_t) i]
                             == values[1][(std::size_t) (start + i)]);
        }

        check(file.reader.readInt32(1000, 30001, dest));
        check(std::equal(left.begin(), left.end(), values[0].begin() + 1000));
        check(std::equal(right.begin(), right.end(), values[1].begin() + 1000));
    }

    constexpr auto doubleFrames = 10000;
    auto doubles =
        std::vector<std::vector<double>>(2, std::vector<double>(doubleFrames));
    auto samples = ByteWriter {};

    for (auto i = 0; i < doubleFrames; ++i)
    {
        for (auto channel = 0; channel < 2; ++channel)
        {
            auto value = (i - 5000) * 1.0e-4 + channel * 0.25 + 1.0e-9;
            doubles[(std::size_t) channel][(std::size_t) i] = value;
            samples.f64(value);
        }
    }

    auto file = OpenedWav(plainWav(3, 2, 64, samples.bytes));
    check(file.ok && file.reader.numFrames() == doubleFrames);

    auto floats = readAll(file.reader);
    auto ints = readAllInt32(file.reader);
    auto allMatch = true;

    for (std::size_t channel = 0; channel < 2; ++channel)
        for (std::size_t i = 0; i < (std::size_t) doubleFrames; ++i)
            allMatch =
                allMatch
                && bitsOf(floats[channel][i]) == bitsOf((float) doubles[channel][i])
                && ints[channel][i] == Files::floatToInt32(doubles[channel][i]);

    check(allMatch, "float64 decodes");

    auto window = std::vector<float>(7);
    float* windowDest[] = {nullptr, window.data()};

    for (auto start: {4096 - 3, 8192 - 3})
    {
        check(file.reader.read(start, 7, windowDest));

        for (auto i = 0; i < 7; ++i)
            check(window[(std::size_t) i]
                  == (float) doubles[1][(std::size_t) (start + i)]);
    }
};

// --- The unpadded size limit --------------------------------------------------

auto tUnpaddedLimit = test("Files/Hardening/unpaddedWritesStopAtTheRiffLimit") = []
{
    for (auto metadataFirst: {true, false})
    {
        auto options = WavWriterOptions {};
        options.numChannels = 1;
        options.bitDepth = BitDepth::pcm16;
        options.headerPadding = false;
        options.metadataBeforeData = metadataFirst;

        auto sink = MemorySink();
        auto writer = WavWriter();
        check(writer.open(sink, options));
        auto headerSize = sink.getData().size();

        // INT_MAX 16-bit frames are 4 GiB of data, past what a RIFF size holds.
        // Refused before a byte is written (the null channel would be silence).
        const float* silent[] = {nullptr};
        check(!writer.write(silent, std::numeric_limits<int>::max()));
        check(writer.isOpen());
        check(sink.getData().size() == headerSize);

        // Refused, not failed: a smaller write still goes through.
        auto one = std::vector<float> {0.5f};
        const float* channels[] = {one.data()};
        check(writer.write(channels, 1));
        check(writer.finish());
        check(sink.getData().size() == headerSize + 2);
        check(readU32(sink.getData(), 4) == headerSize + 2 - 8);
    }
};

// --- Offsets past 2^31 --------------------------------------------------------

auto tLargeOffsets = test("Files/Hardening/fileOffsetsPastTwoGigabytes") = []
{
    // A JUNK chunk just over 2 GiB, then the format and data chunks. Extending the
    // file leaves a hole (sparse where the file system allows), so this costs
    // little disk.
    constexpr auto junk = (std::int64_t {1} << 31) + 4096;

    auto dir = std::filesystem::temp_directory_path() / "MakeASoundFilesTests";
    std::filesystem::create_directories(dir);
    auto path = dir / "hardening-large-offsets.wav";

    {
        auto head = ByteWriter {};
        head.text("RIFF").u32(0).text("WAVE").text("JUNK").u32((std::uint64_t) junk);
        auto sink = Files::FileSink(path);
        check(sink.write(head.bytes));
        check(sink.close());
    }

    auto error = std::error_code();
    std::filesystem::resize_file(path, (std::uintmax_t) (20 + junk), error);
    check(!error, "resize_file");

    auto samples = ByteWriter {};
    samples.u16(0x4000).u16(0x8000).u16(0x2000).u16(0x0001);
    auto tail = ByteWriter {};
    tail.append(riffChunk("fmt ", plainFormat(1, 1, 44100, 16)))
        .append(riffChunk("data", samples.bytes));

    {
        auto out = std::ofstream(path, std::ios::binary | std::ios::app);
        out.write((const char*) tail.bytes.data(),
                  (std::streamsize) tail.bytes.size());
        check(out.good());
    }

    {
        auto source = Files::FileSource(path);
        check(source.isOpen());
        check(source.size() == 20 + junk + (std::int64_t) tail.bytes.size());

        auto id = Bytes(4);
        check(source.read(20 + junk, id) == 4);
        check(textAt(id, 0, 4) == "fmt ");

        auto reader = WavReader();
        check(reader.open(source));
        check(reader.numFrames() == 4);
        check(readAllInt32(reader)
              == (std::vector<std::vector<std::int32_t>> {
                  {0x40000000,
                   std::numeric_limits<std::int32_t>::min(),
                   0x20000000,
                   0x00010000}}));
    }

    std::filesystem::remove(path);
};

// --- Hostile headers ----------------------------------------------------------

Bytes rf64With(std::uint64_t riffSize, std::uint64_t dataSize, const Bytes& after)
{
    auto out = ByteWriter {};
    out.text("RF64").u32(0xffffffff).text("WAVE");
    out.text("ds64").u32(28).u64(riffSize).u64(dataSize).u64(0).u32(0);
    out.append(riffChunk("fmt ", plainFormat(1, 1, 44100, 16)));
    out.text("data").u32(0xffffffff).u16(1).u16(2).u16(3).u16(4);
    out.append(after);
    return out.bytes;
}

auto tHostileRF64 = test("Files/Hardening/hostileRF64Sizes") = []
{
    // A ds64 data size far past the file: only the frames present count.
    for (auto dataSize: {std::uint64_t {1} << 63, ~std::uint64_t {0}})
    {
        auto file = OpenedWav(rf64With(80, dataSize, {}));
        check(file.ok && file.reader.numFrames() == 4);
        check(readAllInt32(file.reader)[0][3] == 4 << 16);
    }

    // A RIFF size of 2^64 - 1: the walk goes to the end of the source.
    auto trkn = riffChunk("Trkn", Bytes {'t', 0});
    auto file = OpenedWav(rf64With(~std::uint64_t {0}, 8, trkn));
    check(file.ok && file.reader.numFrames() == 4);
    check(file.reader.metadata().find<Files::Trkn>() != nullptr);
};

auto tHostileFormats = test("Files/Hardening/hostileFormatChunks") = []
{
    auto data = riffChunk("data", Bytes(8, 0));
    auto opens = [&](const Bytes& format)
    { return OpenedWav(riffFile({riffChunk("fmt ", format), data})).ok; };

    // A sub-format whose first four bytes are not a 16-bit tag.
    for (auto high: {0x00010001u, 0x00010003u, 0xffff0001u})
    {
        std::uint8_t guid[16];
        std::copy(std::begin(pcmGuid), std::end(pcmGuid), guid);
        guid[0] = (std::uint8_t) high;
        guid[1] = (std::uint8_t) (high >> 8);
        guid[2] = (std::uint8_t) (high >> 16);
        guid[3] = (std::uint8_t) (high >> 24);
        check(!opens(extensibleFormat(2, 44100, 16, 3, guid)));
    }

    for (auto bits: {0, 9, 33, 65535})
        check(!opens(plainFormat(1, 1, 44100, bits)));

    for (auto bits: {0, 16, 33})
        check(!opens(plainFormat(3, 1, 44100, bits)));

    // A format chunk declared longer than the file: what is there is enough.
    auto longPlain = ByteWriter {};
    longPlain.append(riffChunk("data", Bytes(4, 0)));
    longPlain.text("fmt ").u32(1000).append(plainFormat(1, 1, 44100, 16));
    auto file = OpenedWav(riffFile({longPlain.bytes}));
    check(file.ok && file.reader.numFrames() == 2);

    auto huge = ByteWriter {};
    huge.append(riffChunk("data", Bytes(4, 0)));
    huge.text("fmt ").u32(0xfffffff0u).append(plainFormat(1, 1, 44100, 16));
    check(OpenedWav(riffFile({huge.bytes})).ok);

    // ... but an extensible tag with 30 of its 40 bytes is not.
    auto shortExtensible = ByteWriter {};
    shortExtensible.append(riffChunk("data", Bytes(4, 0)));
    shortExtensible.text("fmt ").u32(40).append(
        slice(extensibleFormat(1, 44100, 16, 4, pcmGuid), 0, 30));
    check(!OpenedWav(riffFile({shortExtensible.bytes})).ok);
};

auto tHostileAiff = test("Files/Hardening/hostileAiffHeaders") = []
{
    auto samples = aiffSound(Bytes {0x40, 0, 0x20, 0});

    // The channel count is signed: these are negative.
    for (auto channels: {0xffffu, 0x8000u})
        check(!OpenedAiff(aiffFile("AIFF", {aiffCommon(channels, 2, 16), samples}))
                   .ok);

    // An SSND offset past the end: no frames, reads of zeros.
    for (auto offset: {1000u, 0xffffffffu})
    {
        auto file = OpenedAiff(aiffFile(
            "AIFF", {aiffCommon(1, 2, 16), aiffSound(Bytes(4, 0x11), offset)}));
        check(file.ok && file.reader.numFrames() == 0);

        auto out = std::vector<float>(2, 9.f);
        float* dest[] = {out.data()};
        check(file.reader.read(0, 2, dest));
        check(out[0] == 0.f && out[1] == 0.f);
    }

    // A COMM declared far longer than the file, last: its 22 bytes are enough.
    auto common = ByteWriter {};
    common.text("COMM").u32be(0xfffffff0u).u16be(1).u32be(2).u16be(16);
    common.u8(0x40).u8(0x0e).u8(0xac).u8(0x44).zeros(6);
    auto file = OpenedAiff(aiffFile("AIFF", {samples, common.bytes}));
    check(file.ok && file.reader.numFrames() == 2);
};

// --- Corrupt sizes and the bounds on open() -----------------------------------

auto tCorruptSizes =
    test("Files/Hardening/corruptChunkSizesCostOnlyWhatIsThere") = []
{
    // A small file whose last chunk claims nearly 4 GiB.
    auto small = ByteWriter {};
    small.append(riffChunk("fmt ", plainFormat(1, 1, 44100, 16)));
    small.append(riffChunk("data", Bytes(4, 0)));
    small.text("abcd").u32(0xfffffff0u).append(patterned(10));

    auto smallSource = MemorySource(riffFile({small.bytes}, 0));
    auto reader = WavReader();
    auto opened = false;
    auto stats = allocationsIn([&] { opened = reader.open(smallSource); });
    check(opened && reader.numFrames() == 2);

    auto* raw = reader.metadata().find<Files::RawChunk>();
    check(raw != nullptr && raw->id == Files::FourCC("abcd")
          && raw->bytes == patterned(10));
    check(stats.count > 0, "open() allocates, and the watch sees it");
    check(stats.largest < (1u << 20), "largest allocation in open()");
    check(stats.total < (4u << 20), "total allocated in open()");

    // A source of over 4 GiB whose first chunk really is that long: skipped
    // without being read; the format and data after it still count.
    constexpr auto hugeSize = std::int64_t {0xfffffff0u};
    auto sparse = SparseSource();
    sparse.runs.push_back({0,
                           ByteWriter {}
                               .text("RIFF")
                               .u32(0)
                               .text("WAVE")
                               .text("huge")
                               .u32((std::uint64_t) hugeSize)
                               .bytes});
    auto tail = ByteWriter {};
    tail.append(riffChunk("fmt ", plainFormat(1, 1, 44100, 16)))
        .append(riffChunk("data", Bytes {0, 0x40, 0, 0x20}));
    sparse.runs.push_back({20 + hugeSize, tail.bytes});
    sparse.total = 20 + hugeSize + (std::int64_t) tail.bytes.size();

    auto big = WavReader();
    stats = allocationsIn([&] { opened = big.open(sparse); });
    check(opened && big.numFrames() == 2);
    check(big.metadata().empty());
    check(stats.largest < (1u << 20), "largest allocation, 4 GiB chunk");

    auto values = readAll(big);
    check(values == (Channels {{0.5f, 0.25f}}));

    // A format chunk that really is nearly 4 GiB long: only its first 40 bytes
    // are read.
    auto longFormat = SparseSource();
    auto head = ByteWriter {};
    head.text("RIFF").u32(0).text("WAVE");
    head.append(riffChunk("data", Bytes(4, 0)));
    head.text("fmt ")
        .u32((std::uint64_t) hugeSize)
        .append(plainFormat(1, 1, 44100, 16));
    longFormat.runs.push_back({0, head.bytes});
    longFormat.total = 12 + 12 + 8 + hugeSize;

    auto formatReader = WavReader();
    stats = allocationsIn([&] { opened = formatReader.open(longFormat); });
    check(opened && formatReader.numFrames() == 2);
    check(stats.largest < (1u << 20), "largest allocation, 4 GiB format chunk");

    // A source whose size() claims 1 GiB but holds a few bytes, with a 15 MiB
    // chunk (under the cap, so read): only the bytes that arrive are held.
    auto liar = ShrinkingSource(riffFile({small.bytes}, 0));
    auto liarChunk = ByteWriter {};
    liar.bytes.resize(liar.bytes.size() - 18); // drop the corrupt chunk
    liarChunk.text("abcd").u32(15u << 20).append(patterned(100));
    liar.bytes.insert(
        liar.bytes.end(), liarChunk.bytes.begin(), liarChunk.bytes.end());
    liar.length = std::int64_t {1} << 30;

    auto liarReader = WavReader();
    stats = allocationsIn([&] { opened = liarReader.open(liar); });
    check(opened && liarReader.numFrames() == 2);
    raw = liarReader.metadata().find<Files::RawChunk>();
    check(raw != nullptr && raw->bytes == patterned(100));
    check(stats.largest < (1u << 20),
          "largest allocation, source shorter than it says");

    // A metadata chunk over 16 MiB is skipped, not kept cut; the next one is kept.
    auto oversized =
        OpenedWav(riffFile({riffChunk("fmt ", plainFormat(1, 1, 44100, 16)),
                            riffChunk("data", Bytes(2, 0)),
                            riffChunk("iXML", Bytes((16u << 20) + 2, 'x')),
                            riffChunk("Trkn", Bytes {'o', 'k'})}));
    check(oversized.ok);
    check(oversized.reader.metadata().chunks.size() == 1);
    check(oversized.reader.metadata().find<Files::Trkn>() != nullptr);
};

auto tCaps = test("Files/Hardening/metadataCountsAreCapped") = []
{
    constexpr auto many = 5000;
    constexpr auto cap = 4096;

    // Many empty unknown chunks: the first 4096 are kept, the data after them is
    // still found.
    auto chunks =
        std::vector<Bytes> {riffChunk("fmt ", plainFormat(1, 1, 44100, 16))};

    for (auto i = 0; i < many; ++i)
        chunks.push_back(riffChunk("xyz1", {}));

    chunks.push_back(riffChunk("data", Bytes(4, 0)));
    auto file = OpenedWav(riffFile(chunks));
    check(file.ok && file.reader.numFrames() == 2);
    check(file.reader.metadata().chunks.size() == (std::size_t) cap);

    // A LIST/INFO with many entries keeps the first 4096.
    auto info = ByteWriter {};
    info.text("INFO");

    for (auto i = 0; i < many; ++i)
        info.text("IART").u32(2).text("a").zeros(1);

    auto infoFile =
        OpenedWav(riffFile({riffChunk("fmt ", plainFormat(1, 1, 44100, 16)),
                            riffChunk("data", Bytes(2, 0)),
                            riffChunk("LIST", info.bytes)}));
    auto* list = infoFile.reader.metadata().find<Files::ListInfo>();
    check(infoFile.ok && list != nullptr
          && list->entries.size() == (std::size_t) cap);

    // In an adtl list only the entries kept count toward the cap.
    auto adtl = ByteWriter {};
    adtl.text("adtl");

    for (auto i = 0; i < many; ++i)
        adtl.text("file").u32(4).u32(0);

    for (auto i = 0; i < many; ++i)
        adtl.text("labl").u32(6).u32((std::uint64_t) i).text("x").zeros(1);

    auto adtlFile =
        OpenedWav(riffFile({riffChunk("fmt ", plainFormat(1, 1, 44100, 16)),
                            riffChunk("data", Bytes(2, 0)),
                            riffChunk("LIST", adtl.bytes)}));
    auto* labels = adtlFile.reader.metadata().find<Files::AssociatedData>();
    check(labels != nullptr && labels->labels.size() == (std::size_t) cap);
    check(labels != nullptr
          && labels->labels.back().cuePointId == (std::uint32_t) cap - 1);
};

// --- finish() failing late ----------------------------------------------------

auto tFinishFailures =
    test("Files/Hardening/finishFailsOnThePadAndTrailingMetadata") = []
{
    auto options = WavWriterOptions {};
    options.numChannels = 1;
    options.bitDepth = BitDepth::pcm8;
    auto three = std::vector<float> {0.f, 0.f, 0.f};
    const float* channels[] = {three.data()};

    // Takes the header and three bytes of data, refuses the pad.
    {
        auto sink = LimitedSink(104 + 3);
        auto writer = WavWriter();
        check(writer.open(sink, options));
        check(writer.write(channels, 3));
        check(!writer.finish());
        check(!writer.isOpen());

        // The header was still patched.
        check(readU32(sink.getData(), 100) == 3);
    }

    // Takes the data and its pad, refuses the metadata after them.
    {
        options.metadataBeforeData = false;
        options.metadata.chunks.push_back(Files::Trkn {"after"});
        auto sink = LimitedSink(104 + 4);
        auto writer = WavWriter();
        check(writer.open(sink, options));
        check(writer.write(channels, 3));
        check(!writer.finish());
        check(!writer.isOpen());
        check(readU32(sink.getData(), 100) == 3);
    }
};

auto tDestructorSwallows =
    test("Files/Hardening/destructorSwallowsSinkExceptions") = []
{
    auto sink = ThrowingSink();
    auto threw = false;

    try
    {
        auto writer = WavWriter();
        auto options = WavWriterOptions {};
        options.numChannels = 1;
        check(writer.open(sink, options));
        auto one = std::vector<float> {0.5f};
        const float* channels[] = {one.data()};
        check(writer.write(channels, 1));

        // The header rewrite in the destructor's finish() throws; it must not
        // leave the destructor.
        sink.armed = true;
    }
    catch (...)
    {
        threw = true;
    }

    check(!threw);
};
} // namespace
} // namespace FilesHardeningTests
