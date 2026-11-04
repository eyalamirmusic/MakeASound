// Tests for Files::AiffReader on AIFF and AIFF-C files built byte by byte: the
// integer depths in both byte orders, the float codecs, the 80-bit sample rate,
// SSND offsets, truncation, and what fails to open.

#include "FilesTestHelpers.h"

#include <array>
#include <limits>

namespace FilesAiffReaderTests
{
using namespace nano;
using namespace FilesTestHelpers;

using Files::AiffReader;
using Files::MemorySource;

namespace
{
using Rate = std::array<std::uint8_t, 10>;

constexpr auto rate44100 = Rate {0x40, 0x0e, 0xac, 0x44, 0, 0, 0, 0, 0, 0};
constexpr auto rate48000 = Rate {0x40, 0x0e, 0xbb, 0x80, 0, 0, 0, 0, 0, 0};

// COMM: channels, frames, sample size, rate, and for AIFF-C the compression type
// and an empty name.
Bytes comm(int channels,
           std::uint32_t frames,
           int sampleSize,
           const Rate& rate,
           std::string_view compression = {})
{
    auto out = ByteWriter {};
    out.u16be((std::uint32_t) channels & 0xffff).u32be(frames)
        .u16be((std::uint32_t) sampleSize & 0xffff);

    for (auto b: rate)
        out.u8(b);

    if (!compression.empty())
        out.text(compression).u8(0).u8(0); // a Pascal string of length 0, padded

    return aiffChunk("COMM", out.bytes);
}

Bytes ssnd(const Bytes& samples, std::uint32_t offset = 0)
{
    auto out = ByteWriter {};
    out.u32be(offset).u32be(0).zeros((int) offset).append(samples);
    return aiffChunk("SSND", out.bytes);
}

struct Opened
{
    explicit Opened(const Bytes& bytes)
        : source(Bytes(bytes))
    {
        ok = reader.open(source);
    }

    MemorySource source;
    AiffReader reader;
    bool ok = false;
};

// --- Integer PCM --------------------------------------------------------------

auto tSixteenBitStereo = test("Files/Aiff/aiffSixteenBitBigEndian") = []
{
    auto samples = ByteWriter {};
    samples.u16be(0x4000).u16be(0x8000).u16be(0xffff).u16be(0x7fff);

    auto file = Opened(aiffFile("AIFF", {comm(2, 2, 16, rate44100), ssnd(samples.bytes)}));
    check(file.ok && file.reader.isOpen());
    check(file.reader.numChannels() == 2);
    check(file.reader.numFrames() == 2);
    check(file.reader.sampleRate() == 44100.0);
    check(file.reader.bitDepth() == 16);
    check(!file.reader.isFloat());

    check(readAll(file.reader) == (Channels {{0.5f, -1.f / 32768.f}, {-1.0f, 32767.f / 32768.f}}));

    auto ints = readAllInt32(file.reader);
    check(ints[0] == (std::vector<std::int32_t> {0x40000000, -65536}));
    check(ints[1] == (std::vector<std::int32_t> {std::numeric_limits<std::int32_t>::min(),
                                                 32767 * 65536}));
};

auto tEightBitSigned = test("Files/Aiff/eightBitIsSigned") = []
{
    auto file = Opened(aiffFile("AIFF", {comm(1, 4, 8, rate44100),
                                         ssnd(Bytes {0x80, 0x00, 0x7f, 0xff})}));
    check(file.ok && file.reader.bitDepth() == 8);
    check(readAll(file.reader)[0]
          == (std::vector<float> {-1.0f, 0.0f, 127.f / 128.f, -1.f / 128.f}));
    check(readAllInt32(file.reader)[0]
          == (std::vector<std::int32_t> {std::numeric_limits<std::int32_t>::min(), 0,
                                         127 << 24, -16777216}));
};

auto tTwentyFourAndThirtyTwo = test("Files/Aiff/twentyFourAndThirtyTwoBit") = []
{
    auto s24 = ByteWriter {};
    s24.be(0x400000, 3).be(0x800000, 3).be(0x000001, 3);
    auto file = Opened(aiffFile("AIFF", {comm(1, 3, 24, rate48000), ssnd(s24.bytes)}));
    check(file.ok && file.reader.bitDepth() == 24 && file.reader.sampleRate() == 48000.0);
    check(readAll(file.reader)[0] == (std::vector<float> {0.5f, -1.0f, 1.f / 8388608.f}));

    auto s32 = ByteWriter {};
    s32.u32be(0x7fffffff).u32be(0x80000000).u32be(1);
    auto wide = Opened(aiffFile("AIFF", {comm(1, 3, 32, rate48000), ssnd(s32.bytes)}));
    check(wide.ok && wide.reader.bitDepth() == 32);
    check(readAllInt32(wide.reader)[0]
          == (std::vector<std::int32_t> {0x7fffffff, std::numeric_limits<std::int32_t>::min(),
                                         1}));
    auto floats = readAll(wide.reader)[0];
    check(floats[0] == 1.0f && floats[1] == -1.0f && bitsOf(floats[2]) == 0x30000000);
};

// --- AIFF-C codecs -----------------------------------------------------------

auto tAifcBigEndian = test("Files/Aiff/aifcNoneAndTwos") = []
{
    auto samples = ByteWriter {};
    samples.u16be(0x2000).u16be(0xe000);

    for (auto codec: {"NONE", "twos"})
    {
        auto file = Opened(aiffFile("AIFC", {comm(1, 2, 16, rate44100, codec),
                                             ssnd(samples.bytes)}));
        check(file.ok);
        check(readAll(file.reader)[0] == (std::vector<float> {0.25f, -0.25f}));
    }
};

auto tSowt = test("Files/Aiff/sowtIsLittleEndian") = []
{
    auto s16 = ByteWriter {};
    s16.u16(0x2000).u16(0xe000);
    auto file = Opened(aiffFile("AIFC", {comm(1, 2, 16, rate44100, "sowt"), ssnd(s16.bytes)}));
    check(file.ok && !file.reader.isFloat());
    check(readAll(file.reader)[0] == (std::vector<float> {0.25f, -0.25f}));

    auto s24 = ByteWriter {};
    s24.le(0x200000, 3).le(0xe00000, 3);
    auto wide = Opened(aiffFile("AIFC", {comm(1, 2, 24, rate44100, "sowt"), ssnd(s24.bytes)}));
    check(wide.ok && wide.reader.bitDepth() == 24);
    check(readAll(wide.reader)[0] == (std::vector<float> {0.25f, -0.25f}));
};

auto tFloatCodecs = test("Files/Aiff/floatCodecs") = []
{
    auto f32 = ByteWriter {};
    f32.f32be(1.5f).f32be(-0.125f);

    for (auto codec: {"fl32", "FL32"})
    {
        auto file = Opened(aiffFile("AIFC", {comm(1, 2, 32, rate44100, codec), ssnd(f32.bytes)}));
        check(file.ok && file.reader.isFloat() && file.reader.bitDepth() == 32);
        check(readAll(file.reader)[0] == (std::vector<float> {1.5f, -0.125f}));
        check(readAllInt32(file.reader)[0]
              == (std::vector<std::int32_t> {0x7fffffff, -268435456}));
    }

    auto f64 = ByteWriter {};
    f64.f64be(0.1).f64be(-2.0);

    for (auto codec: {"fl64", "FL64"})
    {
        auto file = Opened(aiffFile("AIFC", {comm(1, 2, 64, rate44100, codec), ssnd(f64.bytes)}));
        check(file.ok && file.reader.isFloat() && file.reader.bitDepth() == 64);
        auto floats = readAll(file.reader)[0];
        check(bitsOf(floats[0]) == 0x3dcccccd && floats[1] == -2.0f);
        check(readAllInt32(file.reader)[0][0] == 214748365);
    }

    // The float codecs set the depth whatever the sample size says.
    auto file = Opened(aiffFile("AIFC", {comm(1, 2, 16, rate44100, "fl32"), ssnd(f32.bytes)}));
    check(file.ok && file.reader.bitDepth() == 32 && file.reader.numFrames() == 2);
};

auto tCompressionPlacement = test("Files/Aiff/whereTheCompressionTypeIsRead") = []
{
    auto samples = ByteWriter {};
    samples.u16be(0x4000);

    // AIFF-C with an 18-byte COMM: big-endian PCM.
    auto file = Opened(aiffFile("AIFC", {comm(1, 1, 16, rate44100), ssnd(samples.bytes)}));
    check(file.ok && readAll(file.reader)[0][0] == 0.5f);

    // Plain AIFF never reads a compression type, even from a longer COMM.
    auto plain = Opened(aiffFile("AIFF", {comm(1, 1, 16, rate44100, "sowt"), ssnd(samples.bytes)}));
    check(plain.ok && readAll(plain.reader)[0][0] == 0.5f);
};

auto tUnsupported = test("Files/Aiff/unsupportedFilesFailToOpen") = []
{
    auto data = ssnd(Bytes(8, 0));

    check(!Opened(aiffFile("AIFC", {comm(1, 4, 16, rate44100, "ima4"), data})).ok);
    check(!Opened(aiffFile("AIFC", {comm(1, 4, 16, rate44100, "ulaw"), data})).ok);
    check(!Opened(aiffFile("AIFF", {comm(1, 4, 12, rate44100), data})).ok);
    check(!Opened(aiffFile("AIFF", {comm(1, 4, 64, rate44100), data})).ok);
    check(!Opened(aiffFile("AIFF", {comm(0, 4, 16, rate44100), data})).ok);
    check(!Opened(aiffFile("AIFF", {comm(1, 4, 16, Rate {}), data})).ok); // rate 0
    check(!Opened(aiffFile("AIFF", {comm(1, 4, 16, Rate {0x7f, 0xff, 0x80}), data})).ok);
    check(!Opened(aiffFile("AIFF", {comm(1, 4, 16, Rate {0xc0, 0x0e, 0xac, 0x44}), data})).ok);
    check(!Opened(aiffFile("AIFF", {data})).ok); // no COMM
    check(!Opened(aiffFile("WAVE", {comm(1, 4, 16, rate44100), data})).ok);
    check(!Opened(Bytes(11, 0)).ok);

    auto notForm = aiffFile("AIFF", {comm(1, 4, 16, rate44100), data});
    notForm[0] = 'X';
    check(!Opened(notForm).ok);

    auto closed = Opened(notForm);
    check(!closed.reader.isOpen() && closed.reader.numFrames() == 0);
    check(closed.reader.numChannels() == 0 && closed.reader.sampleRate() == 0.0);

    check(Opened(aiffFile("AIFF", {comm(1, 4, 16, rate44100), data})).ok);
};

// --- The 80-bit rate ----------------------------------------------------------

auto tRates = test("Files/Aiff/eightyBitSampleRates") = []
{
    struct Case
    {
        Rate bytes;
        double rate;
    };

    for (auto c: {Case {{0x40, 0x0b, 0xfa, 0x00}, 8000.0},
                  Case {{0x40, 0x0c, 0xac, 0x44}, 11025.0},
                  Case {{0x40, 0x0d, 0xac, 0x44}, 22050.0},
                  Case {rate44100, 44100.0},
                  Case {rate48000, 48000.0},
                  Case {{0x40, 0x0f, 0xbb, 0x80}, 96000.0},
                  Case {{0x40, 0x10, 0xbb, 0x80}, 192000.0},
                  Case {{0x3f, 0xff, 0x80}, 1.0},
                  Case {{0x40, 0x0d, 0xad, 0xdd, 0x17, 0x45, 0xd1, 0x74, 0x58, 0x00},
                        22254.545454545452}})
    {
        auto file = Opened(aiffFile("AIFF", {comm(1, 1, 16, c.bytes), ssnd(Bytes(2, 0))}));
        check(file.ok);
        check(file.reader.sampleRate() == c.rate);
    }
};

// --- Layout and truncation ----------------------------------------------------

auto tOffsetAndOrder = test("Files/Aiff/ssndOffsetAndChunkOrder") = []
{
    auto samples = ByteWriter {};
    samples.u16be(0x4000).u16be(0x2000);

    auto file = Opened(aiffFile("AIFF",
                                {aiffChunk("FVER", ByteWriter {}.u32be(0xa2805140).bytes),
                                 ssnd(samples.bytes, 6),
                                 aiffChunk("MARK", Bytes {0, 0, 1}), // odd: padded
                                 aiffChunk("INST", Bytes(20, 0)),
                                 comm(1, 2, 16, rate44100)}));
    check(file.ok && file.reader.numFrames() == 2);
    check(readAll(file.reader)[0] == (std::vector<float> {0.5f, 0.25f}));
};

auto tFrameCounts = test("Files/Aiff/framesAreTheSmallerOfCommAndData") = []
{
    auto samples = ByteWriter {};
    samples.u16be(0x4000).u16be(0x2000).u16be(0x1000);

    auto fewer = Opened(aiffFile("AIFF", {comm(1, 2, 16, rate44100), ssnd(samples.bytes)}));
    check(fewer.ok && fewer.reader.numFrames() == 2);

    auto more = Opened(aiffFile("AIFF", {comm(1, 100, 16, rate44100), ssnd(samples.bytes)}));
    check(more.ok && more.reader.numFrames() == 3);

    // Cut inside the last frame.
    auto bytes = aiffFile("AIFF", {comm(1, 3, 16, rate44100), ssnd(samples.bytes)});
    auto cut = Opened(slice(bytes, 0, bytes.size() - 1));
    check(cut.ok && cut.reader.numFrames() == 2);

    auto noData = Opened(aiffFile("AIFF", {comm(1, 3, 16, rate44100)}));
    check(noData.ok && noData.reader.numFrames() == 0);
};

auto tFormSizeZero = test("Files/Aiff/formSizeZeroWalksToTheEnd") = []
{
    auto bytes = aiffFile("AIFF", {comm(1, 1, 16, rate44100), ssnd(Bytes {0x40, 0x00})});
    bytes[4] = bytes[5] = bytes[6] = bytes[7] = 0;
    auto file = Opened(bytes);
    check(file.ok && file.reader.numFrames() == 1);
    check(readAll(file.reader)[0][0] == 0.5f);
};

auto tReadRanges = test("Files/Aiff/readRangesAndArguments") = []
{
    auto samples = ByteWriter {};
    samples.u16be(0x4000).u16be(0x2000);
    auto file = Opened(aiffFile("AIFF", {comm(1, 2, 16, rate44100), ssnd(samples.bytes)}));

    auto out = std::vector<float>(4, 9.f);
    float* dest[] = {out.data()};
    check(file.reader.read(-1, 4, dest));
    check(out == (std::vector<float> {0.f, 0.5f, 0.25f, 0.f}));

    check(!file.reader.read(0, -1, dest));
    check(!file.reader.read(0, 1, nullptr));
    float* none[] = {nullptr};
    check(file.reader.read(0, 1, none));

    auto closed = AiffReader();
    check(!closed.read(0, 1, dest));
    check(!closed.readInt32(0, 1, nullptr));
};
} // namespace
} // namespace FilesAiffReaderTests
