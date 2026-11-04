// Tests for MakeASoundFiles' byte sources and sinks, the float/int sample
// conversions every reader and writer shares, and the FourCC/Metadata helpers.

#include "FilesTestHelpers.h"

#include <filesystem>
#include <limits>

namespace FilesStreamTests
{
using namespace nano;
using namespace FilesTestHelpers;

using Files::FileSink;
using Files::FileSource;
using Files::FourCC;
using Files::MemorySink;
using Files::MemorySource;

namespace
{
std::filesystem::path scratchPath(const std::string& name)
{
    auto dir = std::filesystem::temp_directory_path() / "MakeASoundFilesTests";
    std::filesystem::create_directories(dir);
    return dir / name;
}

Bytes counting(int count)
{
    auto bytes = Bytes((std::size_t) count);

    for (auto i = 0; i < count; ++i)
        bytes[(std::size_t) i] = (std::uint8_t) (i * 7 + 1);

    return bytes;
}

// --- MemorySource -------------------------------------------------------------

auto tMemorySourceReads = test("Files/Streams/memorySourceReadsWithinRange") = []
{
    auto source = MemorySource(counting(10));
    check(source.size() == 10);

    auto dest = Bytes(4, 0xee);
    check(source.read(3, dest) == 4);
    check(dest == slice(counting(10), 3, 4));

    // A read past the end copies what is there and leaves the rest untouched.
    dest.assign(4, 0xee);
    check(source.read(8, dest) == 2);
    check(dest[0] == counting(10)[8] && dest[1] == counting(10)[9]);
    check(dest[2] == 0xee && dest[3] == 0xee);
};

auto tMemorySourceEdges = test("Files/Streams/memorySourceEdgesReadNothing") = []
{
    auto source = MemorySource(counting(10));
    auto dest = Bytes(4, 0xee);

    check(source.read(10, dest) == 0);
    check(source.read(11, dest) == 0);
    check(source.read(-1, dest) == 0);
    check(dest == Bytes(4, 0xee));

    auto empty = Bytes();
    check(source.read(0, empty) == 0);

    auto none = MemorySource();
    check(none.size() == 0);
    check(none.read(0, dest) == 0);
};

auto tMemorySourceViews = test("Files/Streams/memorySourceViewsWithoutCopying") = []
{
    auto bytes = counting(16);
    auto source = MemorySource(MakeASound::Span<const std::uint8_t>(bytes));
    check(source.size() == 16);

    // A view sees later changes to the bytes it views.
    bytes[5] = 0x42;
    auto dest = Bytes(1);
    check(source.read(5, dest) == 1);
    check(dest[0] == 0x42);
};

// --- MemorySink ---------------------------------------------------------------

auto tMemorySinkAppends = test("Files/Streams/memorySinkAppendsAndAdvances") = []
{
    auto sink = MemorySink();
    check(sink.position() == 0);
    check(sink.getData().empty());

    auto first = Bytes {1, 2, 3};
    auto second = Bytes {4, 5};
    check(sink.write(first));
    check(sink.write(second));
    check(sink.position() == 5);
    check(sink.getData() == (Bytes {1, 2, 3, 4, 5}));

    auto empty = Bytes();
    check(sink.write(empty));
    check(sink.position() == 5);
};

auto tMemorySinkOverwrites = test("Files/Streams/memorySinkSeekOverwritesAndExtends") = []
{
    auto sink = MemorySink();
    auto five = Bytes {1, 2, 3, 4, 5};
    check(sink.write(five));

    check(sink.seek(1));
    check(sink.position() == 1);
    auto two = Bytes {9, 9};
    check(sink.write(two));
    check(sink.position() == 3);
    check(sink.getData() == (Bytes {1, 9, 9, 4, 5}));

    // Writing across the end extends.
    check(sink.seek(4));
    auto three = Bytes {7, 7, 7};
    check(sink.write(three));
    check(sink.getData() == (Bytes {1, 9, 9, 4, 7, 7, 7}));
    check(sink.position() == 7);

    // The extent is the furthest position reached.
    check(sink.seek(7));
    check(sink.seek(0));
};

auto tMemorySinkSeekLimits = test("Files/Streams/memorySinkRefusesSeeksOutsideItsExtent") = []
{
    auto sink = MemorySink();
    auto three = Bytes {1, 2, 3};
    check(sink.write(three));

    check(!sink.seek(4));
    check(sink.position() == 3);
    check(!sink.seek(-1));
    check(sink.position() == 3);
};

auto tMemorySinkTake = test("Files/Streams/memorySinkTakeDataEmptiesIt") = []
{
    auto sink = MemorySink();
    auto three = Bytes {1, 2, 3};
    check(sink.write(three));

    auto taken = sink.takeData();
    check(taken == three);
    check(sink.getData().empty());
    check(sink.position() == 0);

    auto one = Bytes {8};
    check(sink.write(one));
    check(sink.getData() == one);
};

// --- FileSource / FileSink ----------------------------------------------------

auto tFileRoundTrip = test("Files/Streams/fileSinkThenFileSource") = []
{
    auto path = scratchPath("streams-roundtrip.bin");

    {
        auto sink = FileSink(path);
        check(sink.isOpen());
        auto bytes = counting(100);
        check(sink.write(bytes));
        check(sink.position() == 100);

        // Patch the start, then return to the end, as a writer does.
        check(sink.seek(2));
        auto patch = Bytes {0xaa, 0xbb};
        check(sink.write(patch));
        check(sink.position() == 4);
        check(sink.seek(100));
        auto tail = Bytes {0xcc};
        check(sink.write(tail));
        check(!sink.seek(102));
        check(sink.close());
        check(!sink.isOpen());
        check(!sink.write(tail));
        check(!sink.seek(0));
    }

    auto expected = counting(100);
    expected[2] = 0xaa;
    expected[3] = 0xbb;
    expected.push_back(0xcc);

    auto source = FileSource(path);
    check(source.isOpen());
    check(source.size() == 101);

    auto all = Bytes(101);
    check(source.read(0, all) == 101);
    check(all == expected);

    auto part = Bytes(10);
    check(source.read(95, part) == 6);
    check(source.read(101, part) == 0);
    check(source.read(-5, part) == 0);

    std::filesystem::remove(path);
};

auto tFileSinkTruncates = test("Files/Streams/fileSinkTruncatesAnExistingFile") = []
{
    auto path = scratchPath("streams-truncate.bin");

    {
        auto sink = FileSink(path);
        auto bytes = counting(50);
        check(sink.write(bytes));
    }

    {
        auto sink = FileSink(path);
        auto bytes = Bytes {1, 2};
        check(sink.write(bytes));
    }

    auto source = FileSource(path);
    check(source.size() == 2);
    std::filesystem::remove(path);
};

auto tFileFailures = test("Files/Streams/missingFilesFailQuietly") = []
{
    auto missing = scratchPath("no-such-dir") / "nested" / "file.bin";

    auto source = FileSource(missing);
    check(!source.isOpen());
    check(source.size() == 0);
    auto dest = Bytes(4);
    check(source.read(0, dest) == 0);

    auto sink = FileSink(missing);
    check(!sink.isOpen());
    check(!sink.write(dest));
    check(!sink.seek(0));
    check(!sink.close());
};

// --- Sample conversions -------------------------------------------------------

auto tFloatToInt32Boundaries = test("Files/Conversion/floatToInt32Boundaries") = []
{
    using Files::floatToInt32;
    constexpr auto lowest = std::numeric_limits<std::int32_t>::min();
    constexpr auto highest = std::numeric_limits<std::int32_t>::max();
    constexpr auto infinity = std::numeric_limits<float>::infinity();

    check(floatToInt32(1.0f) == highest);
    check(floatToInt32(-1.0f) == lowest);
    check(floatToInt32(1.5f) == highest);
    check(floatToInt32(-1.5f) == lowest);
    check(floatToInt32(infinity) == highest);
    check(floatToInt32(-infinity) == lowest);
    check(floatToInt32(std::numeric_limits<float>::quiet_NaN()) == 0);
    check(floatToInt32(0.0f) == 0);
    check(floatToInt32(-0.0f) == 0);
    check(floatToInt32(floatOf(0x3f7fffff)) == 2147483519);
    check(floatToInt32(floatOf(0xbf7fffff)) == -2147483519);
};

auto tFloatToInt32Rounding = test("Files/Conversion/floatToInt32RoundsTheDoubleProduct") = []
{
    using Files::floatToInt32;

    check(floatToInt32(0.5f) == 1073741824); // 1073741823.5, to even
    check(floatToInt32(-0.5f) == -1073741824);
    check(floatToInt32(0.25f) == 536870912); // 536870911.75
    check(floatToInt32(0.75f) == 1610612735); // 1610612735.25
    check(floatToInt32(floatOf(0x3dcccccd)) == 214748368); // 0.1f
    check(floatToInt32(floatOf(0xbdcccccd)) == -214748368);
    check(floatToInt32(0.5f / 32768.f) == 32768); // 32767.99998
    check(floatToInt32(1e-9f) == 2);
    check(floatToInt32(floatOf(0x30000000)) == 1); // 2^-31
};

auto tFloatToInt32Double = test("Files/Conversion/floatToInt32FromDouble") = []
{
    using Files::floatToInt32;

    check(floatToInt32(0.1) == 214748365);
    check(floatToInt32(-0.30000000000000004) == -644245094);
    check(floatToInt32(0.75) == 1610612735);
    check(floatToInt32(2.0) == std::numeric_limits<std::int32_t>::max());
    check(floatToInt32(-1.0) == std::numeric_limits<std::int32_t>::min());
    check(floatToInt32(1.0 - 1.0 / 1099511627776.0) == 2147483647);
    check(floatToInt32(std::numeric_limits<double>::quiet_NaN()) == 0);
};

auto tInt32ToFloat = test("Files/Conversion/int32ToFloatDividesByFullScale") = []
{
    using Files::int32ToFloat;

    check(int32ToFloat(2147483647) == 1.0f);
    check(int32ToFloat(std::numeric_limits<std::int32_t>::min()) == -1.0f);
    check(bitsOf(int32ToFloat(2147483520)) == 0x3f7fffff);
    check(bitsOf(int32ToFloat(2147483583)) == 0x3f7fffff);
    check(int32ToFloat(2147483584) == 1.0f); // a tie, to even
    check(bitsOf(int32ToFloat(2147483392)) == 0x3f7ffffe);
    check(bitsOf(int32ToFloat(1)) == 0x30000000);
    check(bitsOf(int32ToFloat(-1)) == 0xb0000000);
    check(int32ToFloat(1073741824) == 0.5f);
    check(bitsOf(int32ToFloat(123456789)) == 0x3d6b79a3);
    check(bitsOf(int32ToFloat(32767 * 65536)) == 0x3f7ffe00);
    check(int32ToFloat(0) == 0.0f);
};

auto tInt32ToFloatExact = test("Files/Conversion/everyShortIntegerConvertsExactly") = []
{
    using Files::int32ToFloat;

    for (auto s = -32768; s <= 32767; ++s)
        if (int32ToFloat(s * 65536) != (float) s / 32768.f)
        {
            check(false, "16-bit sample not exact");
            break;
        }

    for (auto s = -128; s <= 127; ++s)
        check(int32ToFloat(s * 16777216) == (float) s / 128.f);

    for (auto s: {-8388608, -8388607, -1, 0, 1, 4194304, 8388606, 8388607})
        check(int32ToFloat(s * 256) == (float) ((double) s / 8388608.0));
};

auto tRoundTripDepths = test("Files/Conversion/readThenWrittenAtTheSameDepth") = []
{
    using Files::floatToInt32;
    using Files::int32ToFloat;

    // Scaling by 2^31 - 1 rather than 2^31: a sample read as float and written
    // back at its depth comes back one lower above half scale, else unchanged.
    auto all16 = true;

    for (auto s = -32768; s <= 32767; ++s)
        all16 = all16
                && (floatToInt32(int32ToFloat(s * 65536)) >> 16) == (s > 16384 ? s - 1 : s);

    check(all16, "every 16-bit sample");

    for (auto s = -128; s <= 127; ++s)
        check((floatToInt32(int32ToFloat(s * 16777216)) >> 24) == (s > 64 ? s - 1 : s));

    // 24 bits: 4194305 survives, because 2147483647 * 0x3f000002 rounds (in double)
    // to the tie 1073742079.5, which goes to even.
    struct Case
    {
        int sample, written;
    };

    for (auto c: {Case {-8388608, -8388608}, Case {-4194305, -4194305}, Case {-1, -1},
                  Case {0, 0}, Case {1, 1}, Case {4194303, 4194303},
                  Case {4194304, 4194304}, Case {4194305, 4194305},
                  Case {4194306, 4194305}, Case {8388607, 8388606}})
        check((floatToInt32(int32ToFloat(c.sample * 256)) >> 8) == c.written);

    check(floatToInt32(floatOf(0x3f000002)) == 1073742080);
};

// --- FourCC and Metadata ------------------------------------------------------

auto tFourCC = test("Files/Streams/fourCCWordAndText") = []
{
    constexpr auto data = FourCC("data");
    static_assert(data.asLittleEndian() == 0x61746164);
    static_assert(FourCC::fromLittleEndian(0x61746164) == data);

    check(data.toString() == "data");
    check(FourCC("fmt ").toString() == "fmt ");
    check(FourCC() == FourCC::fromLittleEndian(0));
    check(!(FourCC("LIST") == FourCC("list")));
};

auto tMetadataFind = test("Files/Streams/metadataFindsTheFirstOfAType") = []
{
    auto metadata = Files::Metadata();
    check(metadata.empty());
    check(metadata.find<Files::Smpl>() == nullptr);

    auto info = Files::ListInfo {};
    info.entries.push_back({"IART", "first"});
    auto second = Files::ListInfo {};
    second.entries.push_back({"IART", "second"});

    metadata.chunks.push_back(Files::Inst {});
    metadata.chunks.push_back(info);
    metadata.chunks.push_back(second);

    check(!metadata.empty());
    check(metadata.find<Files::Inst>() != nullptr);
    check(metadata.find<Files::ListInfo>()->entries[0].value == "first");

    metadata.find<Files::ListInfo>()->entries[0].value = "changed";
    check(std::get<Files::ListInfo>(metadata.chunks[1]).entries[0].value == "changed");
};

auto tStructDefaults = test("Files/Streams/chunkDefaults") = []
{
    auto smpl = Files::Smpl {};
    check(smpl.midiUnityNote == 60);
    check(smpl.loops.empty() && smpl.extraBytes.empty());

    auto inst = Files::Inst {};
    check(inst.baseNote == 60 && inst.detune == 0 && inst.gain == 0);
    check(inst.lowNote == 0 && inst.highNote == 127);
    check(inst.lowVelocity == 1 && inst.highVelocity == 127);

    check(Files::CuePoint {}.chunkId == FourCC("data"));
    check(Files::CueRegion {}.purpose == FourCC("rgn "));

    auto options = Files::WavWriterOptions {};
    check(options.headerPadding);
    check(options.metadataBeforeData);
    check(!options.alwaysRF64);
    check(!options.channelMask.has_value());
    check(options.bitDepth == Files::BitDepth::pcm24);
};
} // namespace
} // namespace FilesStreamTests
