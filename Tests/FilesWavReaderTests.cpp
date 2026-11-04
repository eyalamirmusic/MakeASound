// Tests for Files::WavReader on files built byte by byte: every sample format,
// the extensible and RF64 forms, chunk order, truncated and odd files, every
// metadata chunk, the read-range rules, and writer-to-reader round trips.

#include "FilesTestHelpers.h"

#include <filesystem>
#include <limits>

namespace FilesWavReaderTests
{
using namespace nano;
using namespace FilesTestHelpers;

using Files::BitDepth;
using Files::MemorySource;
using Files::WavReader;

namespace
{
constexpr auto int32Min = std::numeric_limits<std::int32_t>::min();
constexpr auto int32Max = std::numeric_limits<std::int32_t>::max();

// A plain-format RIFF file: fmt, then data holding 'samples'.
Bytes simpleWav(int tag, int channels, std::uint32_t rate, int bits, const Bytes& samples)
{
    return riffFile({riffChunk("fmt ", plainFormat(tag, channels, rate, bits)),
                     riffChunk("data", samples)});
}

// Opens 'bytes' in a reader that keeps its own copy of them.
struct Opened
{
    explicit Opened(const Bytes& bytes)
        : source(Bytes(bytes))
    {
        ok = reader.open(source);
    }

    MemorySource source;
    WavReader reader;
    bool ok = false;
};

std::string hexOfFloats(const std::vector<float>& values)
{
    auto out = std::string();
    constexpr auto digits = "0123456789abcdef";

    for (auto value: values)
    {
        auto bits = bitsOf(value);

        for (auto i = 0; i < 4; ++i)
        {
            auto byte = (bits >> (8 * i)) & 0xff;
            out += digits[byte >> 4];
            out += digits[byte & 0xf];
        }
    }

    return out;
}

// --- Sample formats -----------------------------------------------------------

auto tEightBit = test("Files/WavReader/eightBitUnsigned") = []
{
    auto file = Opened(simpleWav(1, 1, 22050, 8, Bytes {0x00, 0x80, 0xff, 0x01, 0x7f}));
    check(file.ok);
    check(file.reader.isOpen());
    check(file.reader.numChannels() == 1);
    check(file.reader.sampleRate() == 22050.0);
    check(file.reader.bitDepth() == 8);
    check(!file.reader.isFloat());
    check(file.reader.numFrames() == 5);
    check(!file.reader.isRF64());
    check(file.reader.channelMask() == 0);
    check(file.reader.metadata().empty());

    auto floats = readAll(file.reader);
    check(floats[0] == (std::vector<float> {-1.0f, 0.0f, 127.f / 128.f, -127.f / 128.f,
                                            -1.f / 128.f}));

    auto ints = readAllInt32(file.reader);
    check(ints[0] == (std::vector<std::int32_t> {int32Min, 0, 127 << 24, -127 * 16777216,
                                                 -16777216}));
};

auto tSixteenBit = test("Files/WavReader/sixteenBit") = []
{
    auto samples = ByteWriter {};

    for (auto s: {-32768, -1, 0, 1, 32767})
        samples.u16((std::uint32_t) s & 0xffff);

    auto file = Opened(simpleWav(1, 1, 44100, 16, samples.bytes));
    check(file.ok && file.reader.bitDepth() == 16 && file.reader.numFrames() == 5);

    auto floats = readAll(file.reader);
    check(floats[0] == (std::vector<float> {-1.0f, -1.f / 32768.f, 0.0f, 1.f / 32768.f,
                                            32767.f / 32768.f}));
    check(bitsOf(floats[0][4]) == 0x3f7ffe00);

    auto ints = readAllInt32(file.reader);
    check(ints[0] == (std::vector<std::int32_t> {int32Min, -65536, 0, 65536, 32767 * 65536}));
};

auto tTwentyFourBit = test("Files/WavReader/twentyFourBit") = []
{
    auto samples = ByteWriter {};

    for (auto s: {-8388608, -1, 0, 1, 8388607, 0x123456})
        samples.le((std::uint32_t) s & 0xffffff, 3);

    auto file = Opened(simpleWav(1, 1, 44100, 24, samples.bytes));
    check(file.ok && file.reader.bitDepth() == 24 && file.reader.numFrames() == 6);

    auto floats = readAll(file.reader);
    check(floats[0] == (std::vector<float> {-1.0f, -1.f / 8388608.f, 0.0f, 1.f / 8388608.f,
                                            8388607.f / 8388608.f,
                                            (float) 0x123456 / 8388608.f}));

    auto ints = readAllInt32(file.reader);
    check(ints[0] == (std::vector<std::int32_t> {int32Min, -256, 0, 256, 8388607 * 256,
                                                 0x12345600}));
};

auto tThirtyTwoBit = test("Files/WavReader/thirtyTwoBitInteger") = []
{
    auto values = std::vector<std::int32_t> {int32Min, -1, 0, 1, int32Max, 2147483583,
                                             2147483584};
    auto samples = ByteWriter {};

    for (auto v: values)
        samples.u32((std::uint32_t) v);

    auto file = Opened(simpleWav(1, 1, 44100, 32, samples.bytes));
    check(file.ok && file.reader.bitDepth() == 32 && !file.reader.isFloat());

    auto floats = readAll(file.reader);
    check(bitsOf(floats[0][0]) == 0xbf800000);
    check(bitsOf(floats[0][1]) == 0xb0000000);
    check(bitsOf(floats[0][2]) == 0);
    check(bitsOf(floats[0][3]) == 0x30000000);
    check(floats[0][4] == 1.0f);
    check(bitsOf(floats[0][5]) == 0x3f7fffff);
    check(floats[0][6] == 1.0f);

    check(readAllInt32(file.reader)[0] == values);
};

auto tFloat32 = test("Files/WavReader/float32BitForBit") = []
{
    auto values = std::vector<float> {1.5f,
                                      -0.0f,
                                      floatOf(0x7fc12345),
                                      -std::numeric_limits<float>::infinity(),
                                      0.25f,
                                      -1.0f};
    auto samples = ByteWriter {};

    for (auto v: values)
        samples.f32(v);

    auto file = Opened(simpleWav(3, 1, 48000, 32, samples.bytes));
    check(file.ok && file.reader.isFloat() && file.reader.bitDepth() == 32);

    auto floats = readAll(file.reader);
    check(sameBits(floats, Channels {values}));

    auto ints = readAllInt32(file.reader);
    check(ints[0] == (std::vector<std::int32_t> {int32Max, 0, 0, int32Min, 536870912,
                                                 int32Min}));
};

auto tFloat64 = test("Files/WavReader/float64RoundsToFloat") = []
{
    auto values = std::vector<double> {0.1, -0.30000000000000004, 1.0 + 1.0 / 1073741824.0,
                                       1e-40, 3.4e39, 0.75, 2.0, -1.0};
    auto samples = ByteWriter {};

    for (auto v: values)
        samples.f64(v);

    auto file = Opened(simpleWav(3, 1, 48000, 64, samples.bytes));
    check(file.ok && file.reader.isFloat() && file.reader.bitDepth() == 64);
    check(file.reader.numFrames() == 8);

    auto floats = readAll(file.reader)[0];
    check(bitsOf(floats[0]) == 0x3dcccccd);
    check(bitsOf(floats[1]) == 0xbe99999a);
    check(bitsOf(floats[2]) == 0x3f800000);
    check(bitsOf(floats[3]) == 0x000116c2);
    check(bitsOf(floats[4]) == 0x7f800000);
    check(floats[5] == 0.75f && floats[6] == 2.0f && floats[7] == -1.0f);

    auto ints = readAllInt32(file.reader)[0];
    check(ints[0] == 214748365);
    check(ints[1] == -644245094);
    check(ints[5] == 1610612735);
    check(ints[6] == int32Max && ints[7] == int32Min);
};

auto tInterleaved = test("Files/WavReader/stereoFramesDeinterleave") = []
{
    auto samples = ByteWriter {};
    samples.u16(1).u16(2).u16(3).u16(4).u16(5).u16(6);

    auto file = Opened(simpleWav(1, 2, 44100, 16, samples.bytes));
    check(file.ok && file.reader.numChannels() == 2 && file.reader.numFrames() == 3);

    auto ints = readAllInt32(file.reader);
    check(ints[0] == (std::vector<std::int32_t> {1 << 16, 3 << 16, 5 << 16}));
    check(ints[1] == (std::vector<std::int32_t> {2 << 16, 4 << 16, 6 << 16}));
};

// --- Extensible ---------------------------------------------------------------

auto tExtensible = test("Files/WavReader/extensibleFormats") = []
{
    auto samples = ByteWriter {};
    samples.le(0x400000, 3).le(0x200000, 3).le(0xc00000, 3);

    auto pcm = Opened(riffFile({riffChunk("fmt ", extensibleFormat(3, 48000, 24, 0x7, pcmGuid)),
                                riffChunk("data", samples.bytes)}));
    check(pcm.ok && !pcm.reader.isFloat() && pcm.reader.bitDepth() == 24);
    check(pcm.reader.channelMask() == 0x7);
    check(pcm.reader.numChannels() == 3 && pcm.reader.numFrames() == 1);
    auto floats = readAll(pcm.reader);
    check(floats[0][0] == 0.5f && floats[1][0] == 0.25f && floats[2][0] == -0.5f);

    // The valid-bits field does not change the decoding.
    auto twenty = Opened(riffFile(
        {riffChunk("fmt ", extensibleFormat(3, 48000, 24, 0x7, pcmGuid, 20)),
         riffChunk("data", samples.bytes)}));
    check(twenty.ok && twenty.reader.bitDepth() == 24);
    check(readAll(twenty.reader)[0][0] == 0.5f);

    auto floatSamples = ByteWriter {};
    floatSamples.f32(0.125f).f32(-2.0f);
    auto flt = Opened(riffFile({riffChunk("fmt ", extensibleFormat(2, 96000, 32, 0x3, floatGuid)),
                                riffChunk("data", floatSamples.bytes)}));
    check(flt.ok && flt.reader.isFloat() && flt.reader.channelMask() == 0x3);
    check(readAll(flt.reader) == (Channels {{0.125f}, {-2.0f}}));
};

auto tAmbisonic = test("Files/WavReader/ambisonicSubFormats") = []
{
    constexpr std::uint8_t ambisonicPcm[16] = {0x01, 0x00, 0x00, 0x00, 0x21, 0x07, 0xd3, 0x11,
                                               0x86, 0x44, 0xc8, 0xc1, 0xca, 0x00, 0x00, 0x00};
    constexpr std::uint8_t ambisonicFloat[16] = {0x03, 0x00, 0x00, 0x00, 0x21, 0x07, 0xd3, 0x11,
                                                 0x86, 0x44, 0xc8, 0xc1, 0xca, 0x00, 0x00, 0x00};

    auto pcm = Opened(riffFile({riffChunk("fmt ", extensibleFormat(4, 48000, 16, 0, ambisonicPcm)),
                                riffChunk("data", Bytes(8, 0))}));
    check(pcm.ok && !pcm.reader.isFloat() && pcm.reader.numFrames() == 1);

    auto flt = Opened(riffFile({riffChunk("fmt ", extensibleFormat(4, 48000, 32, 0, ambisonicFloat)),
                                riffChunk("data", Bytes(16, 0))}));
    check(flt.ok && flt.reader.isFloat());
};

auto tRejectedFormats = test("Files/WavReader/unsupportedFormatsFailToOpen") = []
{
    auto data = riffChunk("data", Bytes(16, 0));
    auto fails = [&](const Bytes& format)
    { return !Opened(riffFile({riffChunk("fmt ", format), data})).ok; };

    check(fails(plainFormat(2, 1, 44100, 4))); // ADPCM
    check(fails(plainFormat(6, 1, 8000, 8))); // A-law
    check(fails(plainFormat(7, 1, 8000, 8))); // mu-law
    check(fails(plainFormat(1, 1, 44100, 12)));
    check(fails(plainFormat(1, 1, 44100, 64)));
    check(fails(plainFormat(3, 1, 44100, 16)));
    check(fails(plainFormat(3, 1, 44100, 24)));
    check(fails(plainFormat(1, 0, 44100, 16))); // no channels
    check(fails(plainFormat(1, 1, 0, 16))); // no rate
    check(fails(slice(plainFormat(1, 1, 44100, 16), 0, 14))); // short

    constexpr std::uint8_t otherGuid[16] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                            0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
    check(fails(extensibleFormat(2, 44100, 16, 3, otherGuid)));
    check(fails(extensibleFormat(2, 44100, 16, 3, floatGuid))); // float at 16 bits
    check(fails(slice(extensibleFormat(2, 44100, 16, 3, pcmGuid), 0, 39)));

    // And the good ones, so the helper is not failing for its own reasons.
    check(!fails(plainFormat(1, 1, 44100, 16)));
    check(!fails(extensibleFormat(2, 44100, 16, 3, pcmGuid)));
};

auto tNotAWav = test("Files/WavReader/notAWavFailsToOpen") = []
{
    auto good = simpleWav(1, 1, 44100, 16, Bytes(4, 0));
    check(Opened(good).ok);

    auto noRiff = good;
    noRiff[0] = 'X';
    check(!Opened(noRiff).ok);

    auto noWave = good;
    noWave[8] = 'X';
    check(!Opened(noWave).ok);

    check(!Opened(slice(good, 0, 11)).ok);
    check(!Opened(Bytes()).ok);
    check(!Opened(riffFile({riffChunk("data", Bytes(4, 0))})).ok); // no fmt

    auto closed = Opened(noRiff);
    check(!closed.reader.isOpen());
    check(closed.reader.numChannels() == 0 && closed.reader.numFrames() == 0);
    check(closed.reader.sampleRate() == 0.0 && closed.reader.bitDepth() == 0);
    check(closed.reader.metadata().empty());
};

auto tNoData = test("Files/WavReader/aFileWithoutDataHasNoFrames") = []
{
    auto file = Opened(riffFile({riffChunk("fmt ", plainFormat(1, 2, 44100, 16)),
                                 riffChunk("Trkn", ByteWriter {}.text("x").zeros(1).bytes)}));
    check(file.ok);
    check(file.reader.numFrames() == 0);
    check(file.reader.metadata().find<Files::Trkn>() != nullptr);
};

// --- RF64 ---------------------------------------------------------------------

Bytes rf64File(std::uint64_t riffSize,
               std::uint64_t dataSize,
               std::uint32_t dataField,
               const Bytes& samples)
{
    auto out = ByteWriter {};
    out.text("RF64").u32(0xffffffff).text("WAVE");
    out.text("ds64").u32(28).u64(riffSize).u64(dataSize).u64(0).u32(0);
    out.append(riffChunk("fmt ", plainFormat(1, 1, 44100, 16)));
    out.text("data").u32(dataField).append(samples);
    return out.bytes;
}

auto tRF64 = test("Files/WavReader/rf64SizesComeFromDs64") = []
{
    auto samples = ByteWriter {};
    samples.u16(1).u16(2).u16(3).u16(4);

    // RIFF size: 4 + 36 + 24 + 8 + 8 = 80.
    auto file = Opened(rf64File(80, 6, 0xffffffff, samples.bytes));
    check(file.ok && file.reader.isRF64());
    check(file.reader.numFrames() == 3);

    // A data size field other than 0xffffffff is used as it is.
    auto direct = Opened(rf64File(80, 6, 4, samples.bytes));
    check(direct.ok && direct.reader.numFrames() == 2);

    // The ds64 data size is clamped to the bytes present.
    auto longer = Opened(rf64File(80, 1000, 0xffffffff, samples.bytes));
    check(longer.ok && longer.reader.numFrames() == 4);
};

auto tRF64Failures = test("Files/WavReader/rf64NeedsALeadingDs64") = []
{
    auto noDs64 = ByteWriter {};
    noDs64.text("RF64").u32(0xffffffff).text("WAVE");
    noDs64.append(riffChunk("fmt ", plainFormat(1, 1, 44100, 16)));
    noDs64.append(riffChunk("data", Bytes(4, 0)));
    check(!Opened(noDs64.bytes).ok);

    auto shortDs64 = ByteWriter {};
    shortDs64.text("RF64").u32(0xffffffff).text("WAVE");
    shortDs64.text("ds64").u32(24).u64(60).u64(4).u64(0);
    shortDs64.append(riffChunk("fmt ", plainFormat(1, 1, 44100, 16)));
    shortDs64.append(riffChunk("data", Bytes(4, 0)));
    check(!Opened(shortDs64.bytes).ok);
};

// --- Chunk order and odd files ------------------------------------------------

auto tChunkOrder = test("Files/WavReader/chunksInAnyOrder") = []
{
    auto samples = ByteWriter {};
    samples.u16(0x4000).u16(0xc000);

    auto file = Opened(riffFile({riffChunk("Trkn", ByteWriter {}.text("a").zeros(1).bytes),
                                 riffChunk("data", samples.bytes),
                                 riffChunk("JUNK", Bytes(10, 0)),
                                 riffChunk("odd!", Bytes {1, 2, 3}),
                                 riffChunk("fact", ByteWriter {}.u32(2).bytes),
                                 riffChunk("PAD ", Bytes(2, 0)),
                                 riffChunk("fmt ", plainFormat(1, 1, 44100, 16)),
                                 riffChunk("acid", Bytes(24, 0))}));
    check(file.ok);
    check(file.reader.numFrames() == 2);
    check(readAll(file.reader) == (Channels {{0.5f, -0.5f}}));

    auto& chunks = file.reader.metadata().chunks;
    check(chunks.size() == 3);
    check(std::holds_alternative<Files::Trkn>(chunks[0]));
    check(std::get<Files::RawChunk>(chunks[1]) == (Files::RawChunk {"odd!", {1, 2, 3}}));
    check(std::holds_alternative<Files::Acid>(chunks[2]));
};

auto tFirstFormatWins = test("Files/WavReader/theFirstFmtAndDataCount") = []
{
    auto file = Opened(riffFile({riffChunk("fmt ", plainFormat(1, 1, 44100, 16)),
                                 riffChunk("data", Bytes(4, 0)),
                                 riffChunk("fmt ", plainFormat(1, 2, 8000, 8)),
                                 riffChunk("data", Bytes(40, 0))}));
    check(file.ok);
    check(file.reader.numChannels() == 1 && file.reader.sampleRate() == 44100.0);
    check(file.reader.numFrames() == 2);
};

auto tTruncatedData = test("Files/WavReader/truncatedDataReadsWhatIsThere") = []
{
    auto samples = ByteWriter {};
    samples.u16(0x4000).u16(0x2000).u16(0x1000);

    // The data chunk claims 100 bytes; 6 are present.
    auto bytes = riffFile({riffChunk("fmt ", plainFormat(1, 1, 44100, 16)),
                           riffChunk("data", samples.bytes, 100)});
    auto file = Opened(bytes);
    check(file.ok && file.reader.numFrames() == 3);
    check(readAll(file.reader) == (Channels {{0.5f, 0.25f, 0.125f}}));

    // A partial last frame is dropped.
    auto partial = Opened(slice(bytes, 0, bytes.size() - 1));
    check(partial.ok && partial.reader.numFrames() == 2);

    // Stereo 24-bit, 2.5 frames present.
    auto stereo = Opened(riffFile({riffChunk("fmt ", plainFormat(1, 2, 44100, 24)),
                                   riffChunk("data", Bytes(15, 0), 600)}));
    check(stereo.ok && stereo.reader.numFrames() == 2);
};

auto tRiffSizes = test("Files/WavReader/wrongRiffSizesAreTolerated") = []
{
    auto chunks = std::vector<Bytes> {riffChunk("fmt ", plainFormat(1, 1, 44100, 16)),
                                      riffChunk("data", Bytes(8, 0)),
                                      riffChunk("Trkn", ByteWriter {}.text("a").zeros(1).bytes)};

    for (auto size: {0ll, 0xffffffffll, 100000ll})
    {
        auto file = Opened(riffFile(chunks, size));
        check(file.ok && file.reader.numFrames() == 4);
        check(file.reader.metadata().find<Files::Trkn>() != nullptr);
    }

    // A RIFF size that ends inside the data: the data is still read in full, the
    // chunk after the declared end is not seen.
    auto shortRiff = Opened(riffFile(chunks, 4 + 24 + 8 + 2));
    check(shortRiff.ok && shortRiff.reader.numFrames() == 4);
    check(shortRiff.reader.metadata().empty());
};

auto tStreamingData = test("Files/WavReader/dataSizeAllOnesMeansToTheEnd") = []
{
    auto bytes = riffFile({riffChunk("fmt ", plainFormat(1, 1, 44100, 16))});
    auto tail = ByteWriter {};
    tail.text("data").u32(0xffffffff).u16(1).u16(2).u16(3).u16(4).u16(5);
    bytes.insert(bytes.end(), tail.bytes.begin(), tail.bytes.end());

    // The RIFF size covers the whole file.
    auto riffSize = (std::uint32_t) bytes.size() - 8;

    for (auto i = 0; i < 4; ++i)
        bytes[(std::size_t) (4 + i)] = (std::uint8_t) (riffSize >> (8 * i));

    auto file = Opened(bytes);
    check(file.ok && file.reader.numFrames() == 5);
};

auto tTrailingGarbage = test("Files/WavReader/aShortTailIsIgnored") = []
{
    // RIFF size 0: walk to the end, where 5 bytes are too few for a chunk header.
    auto bytes = riffFile({riffChunk("fmt ", plainFormat(1, 1, 44100, 16)),
                           riffChunk("data", Bytes(4, 0))},
                          0);
    bytes.insert(bytes.end(), {'a', 'b', 'c', 'd', 'e'});

    auto garbage = Opened(bytes);
    check(garbage.ok && garbage.reader.numFrames() == 2 && garbage.reader.metadata().empty());
};

auto tReopen = test("Files/WavReader/openingAgainForgetsTheLastFile") = []
{
    auto good = MemorySource(riffFile({riffChunk("fmt ", plainFormat(1, 2, 44100, 16)),
                                       riffChunk("data", Bytes(8, 0)),
                                       riffChunk("Trkn", Bytes {'x', 0})}));
    auto bad = MemorySource(Bytes(20, 0));
    auto reader = WavReader();

    check(reader.open(good));
    check(reader.numFrames() == 2 && !reader.metadata().empty());
    check(!reader.open(bad));
    check(!reader.isOpen() && reader.numFrames() == 0 && reader.metadata().empty());

    auto left = std::vector<float>(2, 7.f);
    float* dest[] = {left.data(), left.data()};
    check(!reader.read(0, 2, dest));
    check(left[0] == 7.f);
};

// --- Read ranges --------------------------------------------------------------

auto tReadRanges = test("Files/WavReader/framesOutsideTheFileReadAsZero") = []
{
    auto samples = ByteWriter {};

    for (auto s = 1; s <= 4; ++s)
        samples.u16((std::uint32_t) s * 4096);

    auto file = Opened(simpleWav(1, 1, 44100, 16, samples.bytes));
    auto out = std::vector<float>(8, 9.f);
    float* dest[] = {out.data()};

    check(file.reader.read(-2, 8, dest));
    check(out == (std::vector<float> {0.f, 0.f, 0.125f, 0.25f, 0.375f, 0.5f, 0.f, 0.f}));

    out.assign(8, 9.f);
    check(file.reader.read(3, 2, dest));
    check(out[0] == 0.5f && out[1] == 0.f && out[2] == 9.f);

    out.assign(8, 9.f);
    check(file.reader.read(100, 3, dest));
    check(out[0] == 0.f && out[2] == 0.f && out[3] == 9.f);

    out.assign(8, 9.f);
    check(file.reader.read(-100, 3, dest));
    check(out[0] == 0.f && out[2] == 0.f && out[3] == 9.f);

    auto ints = std::vector<std::int32_t>(3, 5);
    std::int32_t* intDest[] = {ints.data()};
    check(file.reader.readInt32(-1, 3, intDest));
    check(ints == (std::vector<std::int32_t> {0, 4096 << 16, 8192 << 16}));
};

auto tReadArguments = test("Files/WavReader/readArgumentErrors") = []
{
    auto file = Opened(simpleWav(1, 2, 44100, 16, Bytes(8, 0x11)));
    auto left = std::vector<float>(2, 9.f);
    auto right = std::vector<float>(2, 9.f);
    float* both[] = {left.data(), right.data()};
    float* onlyRight[] = {nullptr, right.data()};

    check(file.reader.read(0, 0, both));
    check(left[0] == 9.f);
    check(!file.reader.read(0, -1, both));
    check(!file.reader.read(0, 1, nullptr));
    check(left[0] == 9.f);

    check(file.reader.read(0, 2, onlyRight));
    check(right[0] == (float) 0x1111 / 32768.f);

    std::int32_t* noInts[] = {nullptr, nullptr};
    check(file.reader.readInt32(0, 2, noInts));
    check(!file.reader.readInt32(0, -1, noInts));

    auto closed = WavReader();
    check(!closed.read(0, 1, both));
};

// --- Metadata chunks ----------------------------------------------------------

// A valid file with the given chunks after its format and data chunks.
Opened withChunks(const std::vector<Bytes>& extra)
{
    auto chunks = std::vector<Bytes> {riffChunk("fmt ", plainFormat(1, 1, 44100, 16)),
                                      riffChunk("data", Bytes(2, 0))};
    chunks.insert(chunks.end(), extra.begin(), extra.end());
    return Opened(riffFile(chunks));
}

auto tBextRead = test("Files/WavReader/bextFields") = []
{
    auto p = ByteWriter {};
    p.text("Description").zeros(256 - 11);
    p.text(std::string(32, 'O')); // fills its width: no terminator
    p.text("Ref").zeros(29);
    p.text("2026-11-04");
    p.text("12:00:00");
    p.u64(0x1122334455667788ull).u16(1);
    p.u8(0xab).zeros(63);
    p.zeros(189).u8(0xcd);
    p.text("A=PCM").zeros(3);

    auto file = withChunks({riffChunk("bext", p.bytes)});
    check(file.ok);
    auto* bwav = file.reader.metadata().find<Files::Bwav>();
    check(bwav != nullptr);

    if (bwav == nullptr)
        return;

    check(bwav->description == "Description");
    check(bwav->originator == std::string(32, 'O'));
    check(bwav->originatorReference == "Ref");
    check(bwav->originationDate == "2026-11-04");
    check(bwav->originationTime == "12:00:00");
    check(bwav->timeReference == 0x1122334455667788ull);
    check(bwav->version == 1);
    check(bwav->umid[0] == 0xab && bwav->umid[1] == 0);
    check(bwav->reserved[189] == 0xcd);
    check(bwav->codingHistory == "A=PCM");
};

auto tBextShort = test("Files/WavReader/aShortBextReadsZeros") = []
{
    auto file = withChunks({riffChunk("bext", ByteWriter {}.text("Hi").bytes)});
    auto* bwav = file.reader.metadata().find<Files::Bwav>();
    check(bwav != nullptr);

    if (bwav != nullptr)
    {
        check(bwav->description == "Hi");
        check(bwav->originator.empty() && bwav->timeReference == 0);
        check(bwav->codingHistory.empty());
    }
};

auto tSmplRead = test("Files/WavReader/smplLoopsAndExtraBytes") = []
{
    auto p = ByteWriter {};
    p.u32(1).u32(2).u32(3).u32(64).u32(5).u32(6).u32(7).u32(1).u32(4);
    p.u32(9).u32(0).u32(100).u32(200).u32(0).u32(3);
    p.u8(0xde).u8(0xad);

    auto file = withChunks({riffChunk("smpl", p.bytes)});
    auto* smpl = file.reader.metadata().find<Files::Smpl>();
    check(smpl != nullptr);

    if (smpl == nullptr)
        return;

    auto expected = Files::Smpl {};
    expected.manufacturer = 1;
    expected.product = 2;
    expected.samplePeriod = 3;
    expected.midiUnityNote = 64;
    expected.midiPitchFraction = 5;
    expected.smpteFormat = 6;
    expected.smpteOffset = 7;
    expected.samplerDataSize = 4;
    expected.loops = {{9, 0, 100, 200, 0, 3}};
    expected.extraBytes = {0xde, 0xad};
    check(*smpl == expected);
};

auto tSmplCounts = test("Files/WavReader/smplLoopCountIsWhatFits") = []
{
    // Declares 0 loops but carries 24 more bytes (what the pinned files hold).
    auto zeroLoops = ByteWriter {};
    zeroLoops.zeros(12).u32(60).zeros(20).zeros(24);
    auto file = withChunks({riffChunk("smpl", zeroLoops.bytes)});
    auto* smpl = file.reader.metadata().find<Files::Smpl>();
    check(smpl != nullptr && smpl->loops.empty() && smpl->extraBytes == Bytes(24, 0));
    check(smpl != nullptr && smpl->midiUnityNote == 60);

    // Declares 3 loops, holds 1 and a half.
    auto tooMany = ByteWriter {};
    tooMany.zeros(28).u32(3).u32(0);
    tooMany.u32(1).u32(0).u32(10).u32(20).u32(0).u32(0);
    tooMany.u32(2).u32(0).u32(30);
    auto partial = withChunks({riffChunk("smpl", tooMany.bytes)});
    smpl = partial.reader.metadata().find<Files::Smpl>();
    check(smpl != nullptr && smpl->loops.size() == 1 && smpl->extraBytes.size() == 12);

    // Shorter than its fixed part: the missing fields read 0.
    auto shortSmpl = withChunks({riffChunk("smpl", ByteWriter {}.u32(5).u32(6).bytes)});
    smpl = shortSmpl.reader.metadata().find<Files::Smpl>();
    check(smpl != nullptr && smpl->manufacturer == 5 && smpl->product == 6);
    check(smpl != nullptr && smpl->midiUnityNote == 0 && smpl->loops.empty());
};

auto tInstRead = test("Files/WavReader/instBothCases") = []
{
    auto bytes = Bytes {61, 0xf4, 0xfd, 10, 100, 2, 126};
    auto expected = Files::Inst {61, -12, -3, 10, 100, 2, 126};

    auto lower = withChunks({riffChunk("inst", bytes)});
    check(lower.reader.metadata().find<Files::Inst>() != nullptr);
    check(*lower.reader.metadata().find<Files::Inst>() == expected);

    auto upper = withChunks({riffChunk("INST", bytes)});
    check(upper.reader.metadata().find<Files::Inst>() != nullptr);

    auto shortInst = withChunks({riffChunk("inst", Bytes {70, 1})});
    auto* inst = shortInst.reader.metadata().find<Files::Inst>();
    check(inst != nullptr && *inst == (Files::Inst {70, 1, 0, 0, 0, 0, 0}));
};

auto tCueRead = test("Files/WavReader/cuePoints") = []
{
    auto p = ByteWriter {};
    p.u32(3); // claims 3, holds 2
    p.u32(1).u32(0).text("data").u32(0).u32(0).u32(1000);
    p.u32(2).u32(1).text("data").u32(0).u32(0).u32(2000);

    auto file = withChunks({riffChunk("cue ", p.bytes)});
    auto* cue = file.reader.metadata().find<Files::Cue>();
    check(cue != nullptr);

    if (cue != nullptr)
    {
        check(cue->points.size() == 2);
        check(cue->points[1] == (Files::CuePoint {2, 1, "data", 0, 0, 2000}));
    }
};

auto tInfoRead = test("Files/WavReader/listInfoEntries") = []
{
    auto list = ByteWriter {};
    list.text("info"); // lower case is accepted
    list.text("IART").u32(9).text("Modalics").zeros(1).zeros(1); // odd size, padded
    list.text("icmt").u32(0); // empty, kept, walk goes on
    list.text("ZZZZ").u32(4).text("ab").zeros(2); // unknown keys are kept
    list.text("INAM").u32(200).text("cut"); // runs past the list

    auto file = withChunks({riffChunk("LIST", list.bytes)});
    auto* info = file.reader.metadata().find<Files::ListInfo>();
    check(info != nullptr);

    if (info == nullptr)
        return;

    check(info->entries.size() == 4);
    check(info->entries[0] == (Files::InfoEntry {"IART", "Modalics"}));
    check(info->entries[1] == (Files::InfoEntry {"icmt", ""}));
    check(info->entries[2] == (Files::InfoEntry {"ZZZZ", "ab"}));
    check(info->entries[3] == (Files::InfoEntry {"INAM", "cut"}));
};

auto tAdtlRead = test("Files/WavReader/associatedData") = []
{
    auto list = ByteWriter {};
    list.text("adtl");
    list.text("labl").u32(10).u32(1).text("Intro").zeros(1);
    list.text("note").u32(9).u32(1).text("Odd").zeros(2).zeros(1); // odd size, padded
    list.text("file").u32(4).u32(0); // not read
    list.text("ltxt").u32(26).u32(2).u32(4800).text("rgn ").u16(1).u16(9).u16(1).u16(437)
        .text("Loop").zeros(2);
    list.text("labl").u32(8).u32(2).text("Out").zeros(1);

    auto file = withChunks({riffChunk("LIST", list.bytes)});
    auto* adtl = file.reader.metadata().find<Files::AssociatedData>();
    check(adtl != nullptr);

    if (adtl == nullptr)
        return;

    check(adtl->labels == (std::vector<Files::CueLabel> {{1, "Intro"}, {2, "Out"}}));
    check(adtl->notes == (std::vector<Files::CueLabel> {{1, "Odd"}}));
    check(adtl->regions.size() == 1);
    check(adtl->regions[0] == (Files::CueRegion {2, 4800, "rgn ", 1, 9, 1, 437, "Loop"}));
};

auto tOtherLists = test("Files/WavReader/otherListsAreRaw") = []
{
    auto other = ByteWriter {};
    other.text("exif").u8(1).u8(2);
    auto file = withChunks({riffChunk("LIST", other.bytes), riffChunk("LIST", Bytes {1, 2})});
    auto& chunks = file.reader.metadata().chunks;

    check(chunks.size() == 2);
    check(std::get<Files::RawChunk>(chunks[0]) == (Files::RawChunk {"LIST", other.bytes}));
    check(std::get<Files::RawChunk>(chunks[1]) == (Files::RawChunk {"LIST", {1, 2}}));
};

auto tAcidRead = test("Files/WavReader/acidFields") = []
{
    auto p = ByteWriter {};
    p.u32(0x13).u16(50).u16(7).f32(0.5f).u32(16).u16(4).u16(4).f32(98.5f);

    auto file = withChunks({riffChunk("acid", p.bytes)});
    auto* acid = file.reader.metadata().find<Files::Acid>();
    check(acid != nullptr);
    check(acid != nullptr
          && *acid == (Files::Acid {0x13, 50, 7, 0.5f, 16, 4, 4, 98.5f}));

    auto shortAcid = withChunks({riffChunk("acid", ByteWriter {}.u32(1).bytes)});
    acid = shortAcid.reader.metadata().find<Files::Acid>();
    check(acid != nullptr && acid->flags == 1 && acid->tempo == 0.0f);
};

auto tTextRead = test("Files/WavReader/trknAxmlAndRawChunks") = []
{
    auto file = withChunks(
        {riffChunk("Trkn", ByteWriter {}.text("loop").zeros(2).bytes),
         riffChunk("axml", ByteWriter {}.text("<x/>").zeros(4).bytes),
         riffChunk("iXML", ByteWriter {}.text("<i/>").bytes)});
    auto& chunks = file.reader.metadata().chunks;

    check(chunks.size() == 3);
    check(std::get<Files::Trkn>(chunks[0]).text == "loop");
    check(std::get<Files::Axml>(chunks[1]).xml == "<x/>");
    check(std::get<Files::RawChunk>(chunks[2]).id == Files::FourCC("iXML"));
};

auto tCutChunk = test("Files/WavReader/aChunkCutByTheEndIsReadInPart") = []
{
    auto bytes = riffFile({riffChunk("fmt ", plainFormat(1, 1, 44100, 16)),
                           riffChunk("data", Bytes(2, 0)),
                           riffChunk("Trkn", ByteWriter {}.text("abcdef").bytes, 50)});
    auto file = Opened(bytes);
    check(file.ok);
    auto* trkn = file.reader.metadata().find<Files::Trkn>();
    check(trkn != nullptr && trkn->text == "abcdef");
};

// --- Writer to reader ---------------------------------------------------------

float expectedAfter(BitDepth depth, float x)
{
    auto v = Files::floatToInt32(x);

    switch (depth)
    {
        case BitDepth::pcm8:
            return Files::int32ToFloat((v >> 24) * 16777216);
        case BitDepth::pcm16:
            return Files::int32ToFloat((v >> 16) * 65536);
        case BitDepth::pcm24:
            return Files::int32ToFloat((v >> 8) * 256);
        case BitDepth::pcm32:
            return Files::int32ToFloat(v);
        case BitDepth::float32:
            return x;
    }

    return x;
}

auto tRoundTrips = test("Files/WavReader/writerToReaderPerDepthAndChannelCount") = []
{
    for (auto depth: {BitDepth::pcm8, BitDepth::pcm16, BitDepth::pcm24, BitDepth::pcm32,
                      BitDepth::float32})
    {
        for (auto channels: {1, 2, 3, 6, 9})
        {
            auto input = synthetic(channels, 101, (std::uint32_t) channels + 10);
            auto options = Files::WavWriterOptions {};
            options.numChannels = channels;
            options.sampleRate = 88200.0;
            options.bitDepth = depth;

            auto file = Opened(writeToMemory(options, input));
            check(file.ok);
            check(file.reader.numChannels() == channels);
            check(file.reader.numFrames() == 101);
            check(file.reader.sampleRate() == 88200.0);
            check(file.reader.isFloat() == (depth == BitDepth::float32));

            auto expected = input;

            for (auto& channel: expected)
                for (auto& x: channel)
                    x = expectedAfter(depth, x);

            check(sameBits(readAll(file.reader), expected));
        }
    }
};

auto tMaskRoundTrip = test("Files/WavReader/writtenMasksReadBack") = []
{
    auto options = Files::WavWriterOptions {};
    options.numChannels = 6;
    auto file = Opened(writeToMemory(options, Channels(6, {0.0f})));
    check(file.ok && file.reader.channelMask() == 0x3f);

    options.numChannels = 2;
    options.channelMask = 0x3u;
    auto stereo = Opened(writeToMemory(options, Channels(2, {0.0f})));
    check(stereo.ok && stereo.reader.channelMask() == 0x3);
};

Files::Metadata everyChunk()
{
    auto bwav = Files::Bwav {};
    bwav.description = "Kick";
    bwav.originator = "Modalics";
    bwav.originatorReference = "R1";
    bwav.originationDate = "2026-11-04";
    bwav.originationTime = "10:20:30";
    bwav.timeReference = 48000ull * 3600;
    bwav.version = 1;
    bwav.umid[5] = 9;
    bwav.reserved[0] = 1;
    bwav.codingHistory = "A=PCM,F=48000,W=24,M=stereo";

    auto smpl = Files::Smpl {};
    smpl.midiUnityNote = 36;
    smpl.loops = {{0, 0, 10, 90, 0, 0}, {1, 1, 20, 80, 0, 4}};
    smpl.extraBytes = {1, 2, 3};
    smpl.samplerDataSize = 3;

    auto cue = Files::Cue {};
    cue.points = {{1, 0, "data", 0, 0, 10}, {2, 1, "data", 0, 0, 20}};

    auto adtl = Files::AssociatedData {};
    adtl.labels = {{1, "Start"}, {2, "End"}};
    adtl.notes = {{1, "A note"}};
    auto region = Files::CueRegion {};
    region.cuePointId = 1;
    region.sampleLength = 10;
    region.text = "Region";
    adtl.regions = {region};

    auto info = Files::ListInfo {};
    info.entries = {{"IART", "Modalics"}, {"ICMT", "caf\xc3\xa9"}, {"ISFT", ""}};

    auto metadata = Files::Metadata {};
    metadata.chunks = {bwav,
                       smpl,
                       Files::Inst {48, -5, 2, 36, 60, 1, 127},
                       cue,
                       adtl,
                       info,
                       Files::Acid {0x2, 60, 0, 0.0f, 4, 4, 4, 120.0f},
                       Files::Trkn {"trkn text"},
                       Files::Axml {"<ebu/>"},
                       Files::RawChunk {"iXML", {'<', 'x', '/', '>', '!'}}};
    return metadata;
}

auto tMetadataRoundTrip = test("Files/WavReader/everyChunkRoundTrips") = []
{
    for (auto before: {true, false})
    {
        for (auto padding: {true, false})
        {
            auto options = Files::WavWriterOptions {};
            options.numChannels = 1;
            options.bitDepth = BitDepth::pcm8;
            options.metadata = everyChunk();
            options.metadataBeforeData = before;
            options.headerPadding = padding;

            // 3 frames of 8 bits: odd data, so the trailing metadata follows a pad.
            auto file = Opened(writeToMemory(options, Channels {{0.5f, -0.5f, 0.0f}}));
            check(file.ok);
            check(file.reader.numFrames() == 3);
            check(file.reader.metadata() == everyChunk());
            check(readAll(file.reader) == (Channels {{0.5f, -0.5f, 0.0f}}));
        }
    }
};

auto tRF64RoundTrip = test("Files/WavReader/alwaysRF64RoundTrips") = []
{
    auto options = Files::WavWriterOptions {};
    options.numChannels = 3;
    options.bitDepth = BitDepth::pcm24;
    options.alwaysRF64 = true;
    options.metadata.chunks.push_back(Files::Trkn {"rf64"});

    auto input = synthetic(3, 50, 77);
    auto file = Opened(writeToMemory(options, input));
    check(file.ok && file.reader.isRF64());
    check(file.reader.numFrames() == 50);
    check(file.reader.channelMask() == 0x7);
    check(file.reader.metadata().find<Files::Trkn>() != nullptr);

    for (auto& channel: input)
        for (auto& x: channel)
            x = expectedAfter(BitDepth::pcm24, x);

    check(sameBits(readAll(file.reader), input));
};

auto tFileRoundTrip = test("Files/WavReader/throughARealFile") = []
{
    auto dir = std::filesystem::temp_directory_path() / "MakeASoundFilesTests";
    std::filesystem::create_directories(dir);
    auto path = dir / "reader-roundtrip.wav";
    auto input = synthetic(2, 300, 3);

    {
        auto sink = Files::FileSink(path);
        auto writer = Files::WavWriter();
        auto options = Files::WavWriterOptions {};
        options.bitDepth = BitDepth::float32;
        check(writer.open(sink, options));
        auto pointers = pointersTo(input);
        check(writer.write(pointers.data(), 100));
        const float* rest[] = {input[0].data() + 100, input[1].data() + 100};
        check(writer.write(rest, 200));
        check(writer.finish());
        check(sink.close());
    }

    auto source = Files::FileSource(path);
    auto reader = WavReader();
    check(reader.open(source));
    check(reader.numFrames() == 300);
    check(sameBits(readAll(reader), input));
    std::filesystem::remove(path);
};

auto tGoldenDecode = test("Files/Golden/longMonoRangeReads") = []
{
    auto options = Files::WavWriterOptions {};
    options.numChannels = 1;
    options.sampleRate = 44100.0;
    options.bitDepth = BitDepth::pcm16;

    auto file = Opened(writeToMemory(options, synthetic(1, 2048, 5)));
    check(file.ok && file.reader.numFrames() == 2048);

    auto part = std::vector<float>(16);
    float* dest[] = {part.data()};
    check(file.reader.read(1000, 16, dest));
    check(hexOfFloats(part)
          == "003895be00c06f3e00788cbe00d02ebe006869be00e05a3e00c83fbe0050d23d00e07f3d0054833e"
             "00d0fc3e00807a3d00ce1d3f00584b3e00fcbabe008008bd");

    part.resize(8);
    dest[0] = part.data();
    check(file.reader.read(2040, 8, dest));
    check(hexOfFloats(part)
          == "00d869be003058be00687dbe0060a13e0000e23b00a05c3e00e8023f0032083f");

    struct Sample
    {
        std::int64_t index;
        std::uint32_t bits;
    };

    for (auto s: {Sample {0, 0x3f7ffe00}, Sample {1, 0xbf800000}, Sample {2, 0},
                  Sample {3, 0x3f7ffe00}, Sample {1023, 0x3cde8000},
                  Sample {1024, 0x3f04aa00}, Sample {2047, 0x3f083200}})
    {
        auto value = 0.0f;
        float* one[] = {&value};
        check(file.reader.read(s.index, 1, one));
        check(bitsOf(value) == s.bits);
    }
};
} // namespace
} // namespace FilesWavReaderTests
