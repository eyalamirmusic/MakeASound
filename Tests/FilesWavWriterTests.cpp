// Tests for Files::WavWriter: the header layouts (padded, extensible, unpadded,
// RF64), the sample encodings on boundary values, every metadata chunk's bytes,
// finish() and its failure modes, and the pinned files reproduced from their
// synthetic input.

#include "FilesTestHelpers.h"

#include <algorithm>
#include <limits>

namespace FilesWavWriterTests
{
using namespace nano;
using namespace FilesTestHelpers;

using Files::BitDepth;
using Files::MemorySink;
using Files::WavWriter;
using Files::WavWriterOptions;

namespace
{
WavWriterOptions makeOptions(int channels, double rate, BitDepth depth)
{
    auto options = WavWriterOptions {};
    options.numChannels = channels;
    options.sampleRate = rate;
    options.bitDepth = depth;
    return options;
}

// One frame per value, mono.
Bytes encodeMono(BitDepth depth, const std::vector<float>& values)
{
    auto file = writeToMemory(makeOptions(1, 48000.0, depth), Channels {values});
    check(file.size() >= 104);
    return file.size() >= 104 ? Bytes(file.begin() + 104, file.end()) : Bytes();
}

Bytes encodeMonoInt32(BitDepth depth, const std::vector<std::int32_t>& values)
{
    auto sink = MemorySink();
    auto writer = WavWriter();
    check(writer.open(sink, makeOptions(1, 48000.0, depth)));
    const std::int32_t* channels[] = {values.data()};
    check(writer.writeInt32(channels, (int) values.size()));
    check(writer.finish());
    auto file = sink.takeData();
    return file.size() >= 104 ? Bytes(file.begin() + 104, file.end()) : Bytes();
}

// The bytes of the first metadata chunk of a padded, plain-format file with one
// silent mono 16-bit frame, starting at its id.
Bytes chunkBytes(const Files::Chunk& chunk)
{
    auto options = makeOptions(1, 48000.0, BitDepth::pcm16);
    options.metadata.chunks.push_back(chunk);
    auto file = writeToMemory(options, Channels {{0.0f}});
    check(file.size() > 96 + 8 + 2);

    // The chunk runs from 96 to the 'data' chunk, which is the last 8 + 2 bytes.
    return file.size() > 106 ? Bytes(file.begin() + 96, file.end() - 10) : Bytes();
}

// Keeps the first bytes it is given and only counts the rest, so a test can
// write a file far larger than memory and still read its header back.
class HeaderKeepingSink : public Files::ByteSink
{
public:
    bool write(MakeASound::Span<const std::uint8_t> bytes) override
    {
        for (std::size_t i = 0; i < bytes.getSize(); ++i)
        {
            auto at = writePosition + (std::int64_t) i;

            if (at < (std::int64_t) kept.size())
                kept[(std::size_t) at] = bytes[i];
        }

        writePosition += (std::int64_t) bytes.getSize();
        extent = std::max(extent, writePosition);
        return true;
    }

    bool seek(std::int64_t newPosition) override
    {
        if (newPosition < 0 || newPosition > extent)
            return false;

        writePosition = newPosition;
        return true;
    }

    std::int64_t position() const noexcept override { return writePosition; }

    Bytes kept = Bytes(256);
    std::int64_t writePosition = 0;
    std::int64_t extent = 0;
};

// A sink that refuses every write after the first 'limit' bytes.
class FailingSink : public MemorySink
{
public:
    explicit FailingSink(std::size_t limitToUse)
        : limit(limitToUse)
    {
    }

    bool write(MakeASound::Span<const std::uint8_t> bytes) override
    {
        if ((std::size_t) position() + bytes.getSize() > limit)
            return false;

        return MemorySink::write(bytes);
    }

    std::size_t limit;
};

// --- The plain padded header --------------------------------------------------

// createWaveFile-mono-8's header: RIFF, JUNK (52 zeros), the 16-byte format
// chunk, the data chunk's header.
constexpr std::uint8_t monoEightHeader[104] = {
    0x52, 0x49, 0x46, 0x46, 0x90, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x4a,
    0x55, 0x4e, 0x4b, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x6d, 0x74, 0x20, 0x10, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80, 0xbb, 0x00, 0x00, 0x80, 0xbb, 0x00,
    0x00, 0x01, 0x00, 0x08, 0x00, 0x64, 0x61, 0x74, 0x61, 0x30, 0x00, 0x00, 0x00};

auto tPlainHeader = test("Files/WavWriter/paddedPlainHeaderBytes") = []
{
    auto file = writeToMemory(makeOptions(1, 48000.0, BitDepth::pcm8),
                              synthetic(1, 48, 2));
    check(file.size() == 152);
    check(file.size() >= 104 && std::equal(file.begin(), file.begin() + 104, monoEightHeader));

    // 1, -1, 0, 1.5, -1.5, 1e-9, 2^-16 as unsigned bytes.
    check(slice(file, 104, 7) == (Bytes {0xff, 0x00, 0x80, 0xff, 0x00, 0x80, 0x80}));
};

auto tProvisionalHeader = test("Files/WavWriter/openWritesAValidEmptyFile") = []
{
    auto sink = MemorySink();
    auto writer = WavWriter();
    check(writer.open(sink, makeOptions(2, 44100.0, BitDepth::pcm16)));
    check(writer.isOpen());
    check(writer.framesWritten() == 0);

    auto& bytes = sink.getData();
    check(bytes.size() == 104);
    check(textAt(bytes, 0, 4) == "RIFF" && readU32(bytes, 4) == 96);
    check(textAt(bytes, 96, 4) == "data" && readU32(bytes, 100) == 0);
    check(sink.position() == 104);

    check(writer.finish());
    check(!writer.isOpen());
    check(sink.getData().size() == 104);
    check(readU32(sink.getData(), 4) == 96);
};

auto tFormatFields = test("Files/WavWriter/formatChunkFieldsPerDepth") = []
{
    struct Case
    {
        BitDepth depth;
        int tag, bits, bytes;
    };

    for (auto c: {Case {BitDepth::pcm8, 1, 8, 1},
                  Case {BitDepth::pcm16, 1, 16, 2},
                  Case {BitDepth::pcm24, 1, 24, 3},
                  Case {BitDepth::pcm32, 1, 32, 4},
                  Case {BitDepth::float32, 3, 32, 4}})
    {
        auto file = writeToMemory(makeOptions(2, 96000.0, c.depth),
                                  Channels {{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}});
        check(file.size() == (std::size_t) (104 + 3 * 2 * c.bytes));
        check(readU16(file, 80) == c.tag);
        check(readU16(file, 82) == 2);
        check(readU32(file, 84) == 96000);
        check(readU32(file, 88) == (std::uint32_t) (96000 * 2 * c.bytes));
        check(readU16(file, 92) == 2 * c.bytes);
        check(readU16(file, 94) == c.bits);
        check(readU32(file, 100) == (std::uint32_t) (3 * 2 * c.bytes));
        check(readU32(file, 4) == file.size() - 8);
    }
};

auto tFractionalRate = test("Files/WavWriter/sampleRateStoredAsItsIntegerPart") = []
{
    auto file = writeToMemory(makeOptions(1, 44100.9, BitDepth::pcm16), Channels {{0.f}});
    check(readU32(file, 84) == 44100);
    check(readU32(file, 88) == 88200);
};

// --- Sample encodings ---------------------------------------------------------

const auto boundaryValues = std::vector<float> {1.0f,
                                                -1.0f,
                                                0.0f,
                                                0.5f,
                                                -0.5f,
                                                1.5f,
                                                -1.5f,
                                                0.75f,
                                                floatOf(0xbdcccccd),
                                                0.5f / 32768.f,
                                                floatOf(0x3f7fffff)};

auto tEncode8 = test("Files/WavWriter/eightBitEncoding") = []
{
    auto data = encodeMono(BitDepth::pcm8, boundaryValues);
    check(data == (Bytes {0xff, 0x00, 0x80, 0xc0, 0x40, 0xff, 0x00, 0xdf, 0x73, 0x80, 0xff,
                          0x00})); // the last byte pads the odd data length
};

auto tEncode16 = test("Files/WavWriter/sixteenBitEncoding") = []
{
    auto data = encodeMono(BitDepth::pcm16, boundaryValues);
    auto expected = ByteWriter {};

    for (auto v: {32767, -32768, 0, 16384, -16384, 32767, -32768, 24575, -3277, 0, 32767})
        expected.u16((std::uint32_t) v & 0xffff);

    check(data == expected.bytes);
};

auto tEncode24 = test("Files/WavWriter/twentyFourBitEncoding") = []
{
    auto data = encodeMono(BitDepth::pcm24, boundaryValues);
    auto expected = ByteWriter {};

    for (auto v: {8388607, -8388608, 0, 4194304, -4194304, 8388607, -8388608, 6291455,
                  -838861, 128, 8388607})
        expected.le((std::uint32_t) v & 0xffffff, 3);

    expected.u8(0); // 11 frames of 3 bytes: an odd length, padded
    check(data == expected.bytes);
};

auto tEncode32 = test("Files/WavWriter/thirtyTwoBitIntegerEncoding") = []
{
    auto data = encodeMono(BitDepth::pcm32, boundaryValues);
    auto expected = ByteWriter {};

    for (auto v: {2147483647ll, -2147483648ll, 0ll, 1073741824ll, -1073741824ll,
                  2147483647ll, -2147483648ll, 1610612735ll, -214748368ll, 32768ll,
                  2147483519ll})
        expected.u32((std::uint64_t) v & 0xffffffffu);

    check(data == expected.bytes);
};

auto tEncodeFloat = test("Files/WavWriter/floatSamplesStoredBitForBit") = []
{
    auto values = std::vector<float> {1.5f,
                                      -1.5f,
                                      -0.0f,
                                      std::numeric_limits<float>::infinity(),
                                      floatOf(0x7fc12345), // a NaN with a payload
                                      1e-40f,
                                      0.1f};
    auto data = encodeMono(BitDepth::float32, values);
    auto expected = ByteWriter {};

    for (auto v: values)
        expected.f32(v);

    check(data == expected.bytes);
};

auto tEncodeInt32 = test("Files/WavWriter/writeInt32ShiftsToTheDepth") = []
{
    auto values = std::vector<std::int32_t> {0x7fffffff, (std::int32_t) 0x80000000,
                                             0x12345678, -1, 0x00ffffff};

    check(encodeMonoInt32(BitDepth::pcm8, values)
          == (Bytes {0xff, 0x00, 0x92, 0x7f, 0x80, 0x00}));

    auto sixteen = ByteWriter {};
    sixteen.u16(0x7fff).u16(0x8000).u16(0x1234).u16(0xffff).u16(0x00ff);
    check(encodeMonoInt32(BitDepth::pcm16, values) == sixteen.bytes);

    auto twentyFour = ByteWriter {};
    twentyFour.le(0x7fffff, 3).le(0x800000, 3).le(0x123456, 3).le(0xffffff, 3)
        .le(0x00ffff, 3).u8(0);
    check(encodeMonoInt32(BitDepth::pcm24, values) == twentyFour.bytes);

    auto thirtyTwo = ByteWriter {};

    for (auto v: values)
        thirtyTwo.u32((std::uint32_t) v);

    check(encodeMonoInt32(BitDepth::pcm32, values) == thirtyTwo.bytes);

    auto floats = ByteWriter {};

    for (auto v: values)
        floats.f32(Files::int32ToFloat(v));

    check(encodeMonoInt32(BitDepth::float32, values) == floats.bytes);
};

auto tInterleaving = test("Files/WavWriter/framesInterleaveChannelZeroFirst") = []
{
    auto channels = Channels {{0.5f, -0.5f}, {0.25f, 0.0f}, {-1.0f, 1.0f}};
    auto options = makeOptions(3, 48000.0, BitDepth::pcm16);
    options.channelMask = 0u; // plain header, so the data starts at 104
    auto file = writeToMemory(options, channels);

    auto expected = ByteWriter {};
    expected.u16(16384).u16(8192).u16(0x8000).u16(0xc000).u16(0).u16(0x7fff);
    check(file.size() == 104 + 12);
    check(slice(file, 104, 12) == expected.bytes);
};

auto tNullChannel = test("Files/WavWriter/aNullChannelWritesSilence") = []
{
    for (auto depth: {BitDepth::pcm8, BitDepth::pcm16, BitDepth::float32})
    {
        auto sink = MemorySink();
        auto writer = WavWriter();
        check(writer.open(sink, makeOptions(2, 48000.0, depth)));

        auto left = std::vector<float> {1.0f, 1.0f};
        const float* channels[] = {left.data(), nullptr};
        check(writer.write(channels, 2));
        check(writer.finish());

        auto file = sink.takeData();

        if (depth == BitDepth::pcm8)
            check(slice(file, 104, 4) == (Bytes {0xff, 0x80, 0xff, 0x80}));
        else if (depth == BitDepth::pcm16)
            check(slice(file, 104, 8) == (Bytes {0xff, 0x7f, 0, 0, 0xff, 0x7f, 0, 0}));
        else
            check(readU32(file, 108) == 0 && readU32(file, 116) == 0);
    }
};

auto tOddPad = test("Files/WavWriter/oddDataIsPaddedButNotCounted") = []
{
    auto file = writeToMemory(makeOptions(1, 8000.0, BitDepth::pcm8),
                              Channels {{0.0f, 0.0f, 0.0f}});
    check(file.size() == 108);
    check(readU32(file, 100) == 3); // the data size
    check(file[107] == 0); // the pad
    check(readU32(file, 4) == 100); // the RIFF size counts it
};

// --- Extensible and unpadded headers ------------------------------------------

auto tDefaultMasks = test("Files/WavWriter/defaultChannelMasks") = []
{
    struct Case
    {
        int channels;
        std::uint32_t mask;
    };

    for (auto c: {Case {3, 0x7}, Case {4, 0x33}, Case {5, 0x37}, Case {6, 0x3f},
                  Case {7, 0xf7}, Case {8, 0xff}})
    {
        auto file = writeToMemory(makeOptions(c.channels, 48000.0, BitDepth::pcm24),
                                  Channels((std::size_t) c.channels, {0.0f}));
        check(textAt(file, 12, 4) == "JUNK" && readU32(file, 16) == 28);
        check(textAt(file, 48, 4) == "fmt " && readU32(file, 52) == 40);
        check(readU16(file, 56) == 0xfffe);
        check(readU32(file, 76) == c.mask);
        check(textAt(file, 96, 4) == "data");
    }

    for (auto channels: {1, 2, 9})
    {
        auto file = writeToMemory(makeOptions(channels, 48000.0, BitDepth::pcm24),
                                  Channels((std::size_t) channels, {0.0f}));
        check(readU32(file, 16) == 52 && readU32(file, 76) == 16);
        check(readU16(file, 80) == 1);
    }
};

auto tExtensibleLayout = test("Files/WavWriter/extensibleFormatChunkBytes") = []
{
    auto options = makeOptions(3, 44100.0, BitDepth::float32);
    auto file = writeToMemory(options, Channels {{0.5f}, {0.25f}, {-0.5f}});

    auto expected = ByteWriter {};
    expected.text("RIFF").u32(96 + 12).text("WAVE");
    expected.text("JUNK").u32(28).zeros(28);
    expected.text("fmt ").u32(40).append(extensibleFormat(3, 44100, 32, 0x7, floatGuid));
    expected.text("data").u32(12).f32(0.5f).f32(0.25f).f32(-0.5f);
    check(file == expected.bytes);

    options.bitDepth = BitDepth::pcm16;
    file = writeToMemory(options, Channels {{0.5f}, {0.25f}, {-0.5f}});
    check(slice(file, 48, 48)
          == (ByteWriter {}
                  .text("fmt ")
                  .u32(40)
                  .append(extensibleFormat(3, 44100, 16, 0x7, pcmGuid))
                  .bytes));
};

auto tMaskOverride = test("Files/WavWriter/channelMaskOptionOverridesTheDefault") = []
{
    auto stereo = makeOptions(2, 48000.0, BitDepth::pcm16);
    stereo.channelMask = 0x3u;
    auto file = writeToMemory(stereo, Channels {{0.0f}, {0.0f}});
    check(readU16(file, 56) == 0xfffe && readU32(file, 76) == 0x3);

    auto six = makeOptions(6, 48000.0, BitDepth::pcm16);
    six.channelMask = 0u;
    file = writeToMemory(six, Channels(6, {0.0f}));
    check(readU32(file, 76) == 16 && readU16(file, 80) == 1);
};

auto tUnpadded = test("Files/WavWriter/unpaddedHeaderHasNoJunk") = []
{
    auto options = makeOptions(1, 48000.0, BitDepth::pcm16);
    options.headerPadding = false;
    auto file = writeToMemory(options, Channels {{0.5f}});

    auto expected = ByteWriter {};
    expected.text("RIFF").u32(38).text("WAVE");
    expected.text("fmt ").u32(16).append(plainFormat(1, 1, 48000, 16));
    expected.text("data").u32(2).u16(16384);
    check(file == expected.bytes);

    options.numChannels = 4;
    file = writeToMemory(options, Channels(4, {0.0f}));
    check(textAt(file, 12, 4) == "fmt " && readU32(file, 16) == 40);
    check(textAt(file, 60, 4) == "data");

    options.metadata.chunks.push_back(Files::Trkn {"x"});
    file = writeToMemory(options, Channels(4, {0.0f}));
    check(textAt(file, 60, 4) == "Trkn");
};

// --- RF64 ---------------------------------------------------------------------

auto tAlwaysRF64 = test("Files/WavWriter/alwaysRF64Layout") = []
{
    for (auto padding: {true, false})
    {
        auto options = makeOptions(2, 48000.0, BitDepth::pcm16);
        options.alwaysRF64 = true;
        options.headerPadding = padding;
        auto file = writeToMemory(options, Channels {{0.5f, 0.5f}, {-0.5f, -0.5f}});

        auto expected = ByteWriter {};
        expected.text("RF64").u32(0xffffffff).text("WAVE");
        expected.text("ds64").u32(28).u64(96 + 8).u64(8).u64(0).u32(0);
        expected.text("fmt ").u32(40).append(extensibleFormat(2, 48000, 16, 0, pcmGuid));
        expected.text("data").u32(0xffffffff);
        expected.u16(16384).u16(0xc000).u16(16384).u16(0xc000);
        check(file == expected.bytes);
    }
};

auto tRF64Switch = test("Files/WavWriter/switchesToRF64PastTheRiffLimit") = []
{
    // RIFF size = 96 + data: past 0xffffffff first at 1073741800 float frames.
    constexpr auto frames = 1073741800ll;
    constexpr auto block = 1 << 20;

    auto sink = HeaderKeepingSink();
    auto writer = WavWriter();
    check(writer.open(sink, makeOptions(1, 48000.0, BitDepth::float32)));

    auto zeros = std::vector<float>(block, 0.0f);
    const float* channels[] = {zeros.data()};

    for (auto done = 0ll; done < frames;)
    {
        auto now = (int) std::min<long long>(block, frames - done);

        if (!writer.write(channels, now))
        {
            check(false, "write failed");
            return;
        }

        done += now;
    }

    check(writer.framesWritten() == frames);
    check(writer.finish());

    auto& h = sink.kept;
    check(sink.extent == 104 + frames * 4);
    check(sink.position() == sink.extent);
    check(textAt(h, 0, 4) == "RF64" && readU32(h, 4) == 0xffffffff);
    check(textAt(h, 12, 4) == "ds64" && readU32(h, 16) == 28);
    check(readU64(h, 20) == (std::uint64_t) (96 + frames * 4));
    check(readU64(h, 28) == (std::uint64_t) (frames * 4));
    check(readU64(h, 36) == 0 && readU32(h, 44) == 0);
    check(textAt(h, 48, 4) == "fmt " && readU16(h, 56) == 0xfffe);
    check(slice(h, 80, 16) == Bytes(std::begin(floatGuid), std::end(floatGuid)));
    check(textAt(h, 96, 4) == "data" && readU32(h, 100) == 0xffffffff);
};

// --- Metadata placement -------------------------------------------------------

Files::Metadata libraryMetadata()
{
    auto smpl = Files::Smpl {};
    smpl.extraBytes.assign(24, 0);

    auto info = Files::ListInfo {};
    info.entries = {{"IART", "Modalics"},
                    {"CMNT", "Kick, Dark, caf\xc3\xa9"},
                    {"GENR", "MINDst Drums"},
                    {"ILGT", "0.75"}};

    auto metadata = Files::Metadata {};
    metadata.chunks = {smpl, info};
    return metadata;
}

Bytes libraryMetadataBytes()
{
    auto out = ByteWriter {};
    out.text("smpl").u32(60).zeros(12).u32(60).zeros(20).zeros(24);
    out.text("LIST").u32(84).text("INFO");
    out.text("IART").u32(10).text("Modalics").zeros(2);
    out.text("CMNT").u32(18).text("Kick, Dark, caf\xc3\xa9").zeros(1);
    out.text("GENR").u32(14).text("MINDst Drums").zeros(2);
    out.text("ILGT").u32(6).text("0.75").zeros(2);
    return out.bytes;
}

auto tMetadataBefore = test("Files/WavWriter/metadataBetweenFormatAndData") = []
{
    auto options = makeOptions(2, 44100.0, BitDepth::pcm16);
    options.metadata = libraryMetadata();
    auto file = writeToMemory(options, synthetic(2, 64, 1));

    check(file.size() == 520);
    check(slice(file, 96, 160) == libraryMetadataBytes());
    check(textAt(file, 256, 4) == "data" && readU32(file, 260) == 256);
    check(readU32(file, 4) == 512);
};

auto tMetadataAfter = test("Files/WavWriter/metadataAfterTheDataAndItsPad") = []
{
    auto options = makeOptions(1, 44100.0, BitDepth::pcm8);
    options.metadata = libraryMetadata();
    options.metadataBeforeData = false;

    auto sink = MemorySink();
    auto writer = WavWriter();
    check(writer.open(sink, options));

    // Nothing of the metadata is written before finish().
    check(sink.getData().size() == 104);

    auto samples = std::vector<float> {0.0f, 0.0f, 0.0f};
    const float* channels[] = {samples.data()};
    check(writer.write(channels, 3));
    check(writer.finish());

    auto file = sink.takeData();
    check(file.size() == 104 + 4 + 160);
    check(textAt(file, 96, 4) == "data" && readU32(file, 100) == 3);
    check(file[107] == 0);
    check(slice(file, 108, 160) == libraryMetadataBytes());
    check(readU32(file, 4) == file.size() - 8);
};

// --- Every chunk's bytes ------------------------------------------------------

auto tBextBytes = test("Files/WavWriter/bextLayout") = []
{
    auto bwav = Files::Bwav {};
    bwav.description = "A description";
    bwav.originator = "Modalics";
    bwav.originatorReference = "REF-1";
    bwav.originationDate = "2026-11-04";
    bwav.originationTime = "12:34:56";
    bwav.timeReference = 0x0000000123456789ull;
    bwav.version = 2;
    bwav.umid[0] = 0x11;
    bwav.umid[63] = 0x22;
    bwav.reserved[0] = 0x33;
    bwav.reserved[189] = 0x44;
    bwav.codingHistory = "A=PCM,F=48000";

    auto bytes = chunkBytes(bwav);
    auto payloadSize = (602 + 13 + 1 + 3) / 4 * 4; // 616

    check(textAt(bytes, 0, 4) == "bext");
    check(readU32(bytes, 4) == (std::uint32_t) payloadSize);
    check(bytes.size() == (std::size_t) (8 + payloadSize));

    auto p = slice(bytes, 8, (std::size_t) payloadSize);
    check(textAt(p, 0, 13) == "A description" && p[13] == 0 && p[255] == 0);
    check(textAt(p, 256, 8) == "Modalics" && p[264] == 0);
    check(textAt(p, 288, 5) == "REF-1");
    check(textAt(p, 320, 10) == "2026-11-04");
    check(textAt(p, 330, 8) == "12:34:56");
    check(readU64(p, 338) == 0x0000000123456789ull);
    check(readU16(p, 346) == 2);
    check(p[348] == 0x11 && p[411] == 0x22);
    check(p[412] == 0x33 && p[601] == 0x44);
    check(textAt(p, 602, 13) == "A=PCM,F=48000");
    check(std::all_of(p.begin() + 615, p.end(), [](auto b) { return b == 0; }));
};

auto tBextSizes = test("Files/WavWriter/bextSizeRoundsUpToFour") = []
{
    for (auto [history, size]: std::vector<std::pair<std::string, int>> {
             {"", 604}, {"a", 604}, {"ab", 608}, {"abcde", 608}, {"abcdef", 612}})
    {
        auto bwav = Files::Bwav {};
        bwav.codingHistory = history;
        auto bytes = chunkBytes(bwav);
        check(readU32(bytes, 4) == (std::uint32_t) size);
        check(bytes.size() == (std::size_t) (8 + size));
    }
};

auto tBextTruncation = test("Files/WavWriter/bextTextsCutAtTheirWidth") = []
{
    auto bwav = Files::Bwav {};
    bwav.description = std::string(300, 'a');
    bwav.originator = std::string(31, 'b') + "\xc3\xa9"; // 33 bytes, cut before the é
    bwav.originationDate = "2026-11-04-extra";

    auto p = slice(chunkBytes(bwav), 8, 604);
    check(textAt(p, 0, 256) == std::string(256, 'a'));
    check(textAt(p, 256, 32) == std::string(31, 'b') + std::string(1, '\0'));
    check(textAt(p, 320, 10) == "2026-11-04");
    check(p[330] == 0);
};

auto tSmplBytes = test("Files/WavWriter/smplLayout") = []
{
    auto smpl = Files::Smpl {};
    smpl.manufacturer = 1;
    smpl.product = 2;
    smpl.samplePeriod = 22675;
    smpl.midiUnityNote = 64;
    smpl.midiPitchFraction = 0x80000000;
    smpl.smpteFormat = 25;
    smpl.smpteOffset = 0x01020304;
    smpl.samplerDataSize = 3;
    smpl.loops = {{7, 0, 100, 200, 0, 0}, {8, 1, 300, 400, 5, 2}};
    smpl.extraBytes = {0xaa, 0xbb, 0xcc};

    auto expected = ByteWriter {};
    expected.text("smpl").u32(36 + 48 + 3);
    expected.u32(1).u32(2).u32(22675).u32(64).u32(0x80000000).u32(25).u32(0x01020304)
        .u32(2).u32(3);
    expected.u32(7).u32(0).u32(100).u32(200).u32(0).u32(0);
    expected.u32(8).u32(1).u32(300).u32(400).u32(5).u32(2);
    expected.u8(0xaa).u8(0xbb).u8(0xcc).u8(0); // odd: padded
    check(chunkBytes(smpl) == expected.bytes);
};

auto tInstBytes = test("Files/WavWriter/instLayout") = []
{
    auto inst = Files::Inst {};
    inst.baseNote = 61;
    inst.detune = -12;
    inst.gain = -3;
    inst.lowNote = 10;
    inst.highNote = 100;
    inst.lowVelocity = 2;
    inst.highVelocity = 126;

    check(chunkBytes(inst)
          == (ByteWriter {}
                  .text("inst")
                  .u32(7)
                  .u8(61)
                  .u8(0xf4)
                  .u8(0xfd)
                  .u8(10)
                  .u8(100)
                  .u8(2)
                  .u8(126)
                  .u8(0)
                  .bytes));
};

auto tCueBytes = test("Files/WavWriter/cueLayout") = []
{
    auto cue = Files::Cue {};
    cue.points.push_back({1, 0, "data", 0, 0, 1000});
    cue.points.push_back({2, 1, "slnt", 4, 8, 2000});

    auto expected = ByteWriter {};
    expected.text("cue ").u32(4 + 48).u32(2);
    expected.u32(1).u32(0).text("data").u32(0).u32(0).u32(1000);
    expected.u32(2).u32(1).text("slnt").u32(4).u32(8).u32(2000);
    check(chunkBytes(cue) == expected.bytes);

    check(chunkBytes(Files::Cue {}) == (ByteWriter {}.text("cue ").u32(4).u32(0).bytes));
};

auto tAdtlBytes = test("Files/WavWriter/associatedDataLayout") = []
{
    auto adtl = Files::AssociatedData {};
    adtl.labels.push_back({1, "Intro"}); // 5 + 1: even
    adtl.labels.push_back({2, "Verse"});
    adtl.notes.push_back({1, "Note"}); // 4 + 1: padded to 6
    auto region = Files::CueRegion {};
    region.cuePointId = 2;
    region.sampleLength = 4800;
    region.country = 1;
    region.language = 9;
    region.dialect = 1;
    region.codePage = 437;
    region.text = "Loop";
    adtl.regions.push_back(region);

    auto list = ByteWriter {};
    list.text("adtl");
    list.text("labl").u32(10).u32(1).text("Intro").zeros(1);
    list.text("labl").u32(10).u32(2).text("Verse").zeros(1);
    list.text("note").u32(10).u32(1).text("Note").zeros(2);
    list.text("ltxt").u32(26).u32(2).u32(4800).text("rgn ").u16(1).u16(9).u16(1).u16(437)
        .text("Loop").zeros(2);

    check(chunkBytes(adtl) == riffChunk("LIST", list.bytes));
};

auto tInfoBytes = test("Files/WavWriter/listInfoLayout") = []
{
    auto info = Files::ListInfo {};
    info.entries = {{"INAM", "Kick"}, {"ICMT", "abc"}, {"ISFT", ""}};

    auto list = ByteWriter {};
    list.text("INFO");
    list.text("INAM").u32(6).text("Kick").zeros(2);
    list.text("ICMT").u32(4).text("abc").zeros(1);
    list.text("ISFT").u32(2).zeros(2);
    check(chunkBytes(info) == riffChunk("LIST", list.bytes));

    check(chunkBytes(Files::ListInfo {}) == riffChunk("LIST", ByteWriter {}.text("INFO").bytes));
};

auto tAcidBytes = test("Files/WavWriter/acidLayout") = []
{
    auto acid = Files::Acid {};
    acid.flags = 0x02 | 0x04;
    acid.rootNote = 48;
    acid.reserved1 = 0x8000;
    acid.reserved2 = 0.5f;
    acid.numBeats = 8;
    acid.meterDenominator = 4;
    acid.meterNumerator = 3;
    acid.tempo = 120.5f;

    auto expected = ByteWriter {};
    expected.text("acid").u32(24);
    expected.u32(6).u16(48).u16(0x8000).f32(0.5f).u32(8).u16(4).u16(3).f32(120.5f);
    check(chunkBytes(acid) == expected.bytes);
};

auto tTextChunks = test("Files/WavWriter/trknAxmlAndRawLayout") = []
{
    check(chunkBytes(Files::Trkn {"loop"})
          == (ByteWriter {}.text("Trkn").u32(6).text("loop").zeros(2).bytes));
    check(chunkBytes(Files::Trkn {"odd"})
          == (ByteWriter {}.text("Trkn").u32(4).text("odd").zeros(1).bytes));

    // axml: the bytes verbatim, no terminator, pad when odd.
    check(chunkBytes(Files::Axml {"<a/>"})
          == (ByteWriter {}.text("axml").u32(4).text("<a/>").bytes));
    check(chunkBytes(Files::Axml {"<ab/>"})
          == (ByteWriter {}.text("axml").u32(5).text("<ab/>").u8(0).bytes));

    check(chunkBytes(Files::RawChunk {"iXML", {1, 2, 3}})
          == (ByteWriter {}.text("iXML").u32(3).u8(1).u8(2).u8(3).u8(0).bytes));
    check(chunkBytes(Files::RawChunk {"empt", {}})
          == (ByteWriter {}.text("empt").u32(0).bytes));
};

auto tChunkOrder = test("Files/WavWriter/chunksWrittenInTheGivenOrder") = []
{
    auto options = makeOptions(1, 48000.0, BitDepth::pcm16);
    options.metadata.chunks = {Files::Trkn {"a"}, Files::Inst {}, Files::Axml {"xy"}};
    auto file = writeToMemory(options, Channels {{0.0f}});

    check(textAt(file, 96, 4) == "Trkn");
    check(textAt(file, 96 + 10, 4) == "inst");
    check(textAt(file, 96 + 10 + 16, 4) == "axml");
    check(textAt(file, 96 + 10 + 16 + 10, 4) == "data");
};

// --- Options and call errors --------------------------------------------------

auto tInvalidOptions = test("Files/WavWriter/invalidOptionsWriteNothing") = []
{
    auto attempt = [](WavWriterOptions options)
    {
        auto sink = MemorySink();
        auto writer = WavWriter();
        auto opened = writer.open(sink, options);
        check(!writer.isOpen() || opened);
        return opened || !sink.getData().empty();
    };

    auto base = makeOptions(2, 48000.0, BitDepth::pcm24);
    check(attempt(base));

    auto o = base;
    o.sampleRate = 0.0;
    check(!attempt(o));
    o.sampleRate = 0.5;
    check(!attempt(o));
    o.sampleRate = -44100.0;
    check(!attempt(o));
    o.sampleRate = std::numeric_limits<double>::quiet_NaN();
    check(!attempt(o));
    o.sampleRate = std::numeric_limits<double>::infinity();
    check(!attempt(o));
    o.sampleRate = 4294967296.0;
    check(!attempt(o));
    o.sampleRate = 4294967295.0;
    check(attempt(o));

    o = base;
    o.numChannels = 0;
    check(!attempt(o));
    o.numChannels = 21845; // block align 65535
    check(attempt(o));
    o.numChannels = 21846;
    check(!attempt(o));

    for (auto id: {"fmt ", "data", "ds64"})
    {
        o = base;
        auto raw = Files::RawChunk {};
        std::copy(id, id + 4, raw.id.bytes.begin());
        o.metadata.chunks.push_back(raw);
        check(!attempt(o));
    }
};

auto tCallErrors = test("Files/WavWriter/callsOutOfStateFail") = []
{
    auto writer = WavWriter();
    auto samples = std::vector<float> {0.0f};
    const float* channels[] = {samples.data()};

    check(!writer.isOpen());
    check(!writer.write(channels, 1));
    check(!writer.finish());

    auto sink = MemorySink();
    check(writer.open(sink, makeOptions(1, 48000.0, BitDepth::pcm16)));

    auto other = MemorySink();
    check(!writer.open(other, makeOptions(1, 48000.0, BitDepth::pcm16)));
    check(other.getData().empty());

    check(writer.write(channels, 0));
    check(!writer.write(channels, -1));
    check(!writer.write(nullptr, 1));
    check(writer.write(channels, 1));
    check(writer.framesWritten() == 1);
    check(writer.finish());
    check(writer.framesWritten() == 1);
    check(!writer.write(channels, 1));
    check(!writer.finish());

    // Re-opening after finish starts a new file.
    auto again = MemorySink();
    check(writer.open(again, makeOptions(1, 48000.0, BitDepth::pcm16)));
    check(writer.framesWritten() == 0);
    check(writer.finish());
};

auto tDestructorFinishes = test("Files/WavWriter/destructorFinishesTheFile") = []
{
    auto sink = MemorySink();

    {
        auto writer = WavWriter();
        check(writer.open(sink, makeOptions(1, 48000.0, BitDepth::pcm16)));
        auto samples = std::vector<float> {0.5f, 0.5f};
        const float* channels[] = {samples.data()};
        check(writer.write(channels, 2));
    }

    auto& file = sink.getData();
    check(file.size() == 108);
    check(readU32(file, 4) == 100);
    check(readU32(file, 100) == 4);
};

auto tHeaderOffset = test("Files/WavWriter/headerStartsAtTheSinkPosition") = []
{
    auto sink = MemorySink();
    auto prefix = Bytes {9, 9, 9, 9, 9};
    check(sink.write(prefix));

    auto writer = WavWriter();
    check(writer.open(sink, makeOptions(1, 48000.0, BitDepth::pcm16)));
    auto samples = std::vector<float> {0.5f};
    const float* channels[] = {samples.data()};
    check(writer.write(channels, 1));
    check(writer.finish());

    auto& file = sink.getData();
    check(file.size() == 5 + 106);
    check(slice(file, 0, 5) == prefix);
    check(textAt(file, 5, 4) == "RIFF" && readU32(file, 9) == 98);
    check(textAt(file, 5 + 96, 4) == "data" && readU32(file, 5 + 100) == 2);
    check(sink.position() == (std::int64_t) file.size());
};

auto tSinkFailure = test("Files/WavWriter/aRefusingSinkFailsTheWriter") = []
{
    // Refuses the header.
    {
        auto sink = FailingSink(50);
        auto writer = WavWriter();
        check(!writer.open(sink, makeOptions(1, 48000.0, BitDepth::pcm16)));
        check(!writer.isOpen());
    }

    // Takes the header, refuses the samples.
    {
        auto sink = FailingSink(110);
        auto writer = WavWriter();
        check(writer.open(sink, makeOptions(1, 48000.0, BitDepth::pcm16)));

        auto samples = std::vector<float>(100, 0.0f);
        const float* channels[] = {samples.data()};
        check(!writer.write(channels, 100));
        check(!writer.write(channels, 1));
        check(!writer.finish());
        check(!writer.isOpen());
    }
};

// --- The pinned files ---------------------------------------------------------

struct Golden
{
    const char* name;
    int channels, frames;
    std::uint32_t seed;
    double rate;
    BitDepth depth;
    bool metadata;
    std::size_t size;
    std::uint64_t hash;
};

constexpr Golden goldens[] = {
    {"createWaveFile-mono-8", 1, 48, 2, 48000, BitDepth::pcm8, false, 152, 0x70b4947488e89534ull},
    {"createWaveFile-mono-16", 1, 48, 2, 48000, BitDepth::pcm16, false, 200, 0xcdf6dabf223d38fcull},
    {"createWaveFile-mono-24", 1, 48, 2, 48000, BitDepth::pcm24, false, 248, 0x20968c3f71cbd1a4ull},
    {"createWaveFile-mono-32", 1, 48, 2, 48000, BitDepth::float32, false, 296, 0xf72f0ed1557dffc3ull},
    {"createWaveFile-stereo-8", 2, 64, 1, 48000, BitDepth::pcm8, false, 232, 0x66e00aa4cafd8bdfull},
    {"createWaveFile-stereo-16", 2, 64, 1, 48000, BitDepth::pcm16, false, 360, 0x4ae0af19b27bbe9eull},
    {"createWaveFile-stereo-24", 2, 64, 1, 48000, BitDepth::pcm24, false, 488, 0x1279a2cc5f419423ull},
    {"createWaveFile-stereo-32", 2, 64, 1, 48000, BitDepth::float32, false, 616, 0x7d1433b1aee51dccull},
    {"createWaveFile-metadata-8", 2, 64, 1, 44100, BitDepth::pcm8, true, 392, 0xd16105dd2b85b0c6ull},
    {"createWaveFile-metadata-16", 2, 64, 1, 44100, BitDepth::pcm16, true, 520, 0x33555f85d0419cc5ull},
    {"createWaveFile-metadata-24", 2, 64, 1, 44100, BitDepth::pcm24, true, 648, 0xf6970e68a0bcb7a1ull},
    {"createWaveFile-metadata-32", 2, 64, 1, 44100, BitDepth::float32, true, 776, 0x6c880ffbc3e3363eull},
    {"toMemory-stereo", 2, 64, 1, 48000, BitDepth::pcm24, false, 488, 0x1279a2cc5f419423ull},
    {"toWaveFile-mono-44100", 1, 48, 2, 44100, BitDepth::pcm24, false, 248, 0x4dd0b3477aeefaafull},
    {"long-mono-16", 1, 2048, 5, 44100, BitDepth::pcm16, false, 4200, 0xceb9eb60be289279ull},
};

auto tGoldens = test("Files/Golden/pinnedFilesFromSyntheticInput") = []
{
    for (auto& g: goldens)
    {
        auto options = makeOptions(g.channels, g.rate, g.depth);

        if (g.metadata)
            options.metadata = libraryMetadata();

        auto file = writeToMemory(options, synthetic(g.channels, g.frames, g.seed));
        check(file.size() == g.size, g.name);
        check(fnv1a64(file) == g.hash, g.name);
    }
};

auto tGoldenStereo24 = test("Files/Golden/stereo24FirstFrames") = []
{
    auto file = writeToMemory(makeOptions(2, 48000.0, BitDepth::pcm24), synthetic(2, 64, 1));
    check(slice(file, 104, 12)
          == (Bytes {0xff, 0xff, 0x7f, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80}));
};
} // namespace
} // namespace FilesWavWriterTests
