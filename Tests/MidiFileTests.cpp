// Tests for MakeASound::MIDI::File, the Standard MIDI File reader and writer.
// Every expected value comes from the SMF 1.0 specification (its examples are
// reproduced byte for byte) or from the behaviour specification of this type:
// variable-length quantities, running status in both directions, SysEx and meta
// events, the end-of-track rules, type 0 merging, SMPTE divisions, the tempo map
// arithmetic, the tolerances (short last chunk, trailing bytes) and round trips.

#include <MakeASound/MIDI/MidiFile.h>

#include <NanoTest/NanoTest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace MidiFileTests
{
using namespace nano;
using MakeASound::Span;
using MakeASound::MIDI::File;
using MakeASound::MIDI::smpteTimeFormat;
using MakeASound::MIDI::TempoMap;
using MakeASound::MIDI::TimedMessage;
using MakeASound::MIDI::Track;

namespace
{
using Bytes = std::vector<std::uint8_t>;

// "4d 54 68 64" -> bytes; anything that is not a hex digit separates.
Bytes hex(std::string_view text)
{
    auto result = Bytes {};
    auto high = -1;

    for (auto c: text)
    {
        auto nibble = -1;

        if (c >= '0' && c <= '9')
            nibble = c - '0';
        else if (c >= 'a' && c <= 'f')
            nibble = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            nibble = c - 'A' + 10;

        if (nibble < 0)
            continue;

        if (high < 0)
        {
            high = nibble;
        }
        else
        {
            result.push_back((std::uint8_t) (high * 16 + nibble));
            high = -1;
        }
    }

    return result;
}

Bytes concat(std::initializer_list<Bytes> parts)
{
    auto result = Bytes {};

    for (auto& part: parts)
        result.insert(result.end(), part.begin(), part.end());

    return result;
}

Bytes be32(std::uint32_t value)
{
    return {(std::uint8_t) (value >> 24),
            (std::uint8_t) (value >> 16),
            (std::uint8_t) (value >> 8),
            (std::uint8_t) value};
}

Bytes header(int format, int numTracks, int division)
{
    return concat({hex("4d546864 00000006"),
                   {(std::uint8_t) (format >> 8),
                    (std::uint8_t) format,
                    (std::uint8_t) (numTracks >> 8),
                    (std::uint8_t) numTracks,
                    (std::uint8_t) (division >> 8),
                    (std::uint8_t) division}});
}

// A chunk whose length field says `declared` (the real size by default).
Bytes chunk(std::string_view id, const Bytes& data, long long declared = -1)
{
    auto size =
        declared < 0 ? (std::uint32_t) data.size() : (std::uint32_t) declared;
    auto result = Bytes(id.begin(), id.end());
    auto length = be32(size);
    result.insert(result.end(), length.begin(), length.end());
    result.insert(result.end(), data.begin(), data.end());
    return result;
}

Bytes trackChunk(const Bytes& data)
{
    return chunk("MTrk", data);
}

Bytes vlq(std::uint32_t value)
{
    auto groups = Bytes {(std::uint8_t) (value & 0x7f)};

    while ((value >>= 7) > 0)
        groups.insert(groups.begin(), (std::uint8_t) ((value & 0x7f) | 0x80));

    return groups;
}

Span<const std::uint8_t> view(const Bytes& bytes)
{
    return Span<const std::uint8_t>(bytes.data(), (int) bytes.size());
}

TimedMessage at(double time, const Bytes& bytes)
{
    auto message = TimedMessage {};
    message.time = time;
    message.bytes = bytes;
    return message;
}

Track trackOf(std::initializer_list<TimedMessage> events)
{
    auto track = Track {};
    track.events = events;
    return track;
}

File fileOf(int timeFormat, std::initializer_list<Track> tracks)
{
    auto file = File {};
    file.timeFormat = timeFormat;
    file.tracks = tracks;
    return file;
}

File readOrFail(const Bytes& bytes)
{
    auto file = File {};
    check(file.readFrom(view(bytes)), "readFrom accepts the file");
    return file;
}

bool eventIs(const Track& track, int index, double time, const Bytes& bytes)
{
    return index < (int) track.events.size()
           && track.events[(std::size_t) index].time == time
           && track.events[(std::size_t) index].bytes == bytes;
}

bool bitsAre(double value, std::uint64_t bits)
{
    return std::bit_cast<std::uint64_t>(value) == bits;
}

// The SMF 1.0 specification's example piece, format 0 and format 1.
Bytes smfExampleFormat0()
{
    return hex("4d546864 00000006 0000 0001 0060"
               "4d54726b 0000003b"
               "00ff580404021808 00ff510307a120 00c005 00c12e 00c246"
               "00923060 003c60 60914340 60904c20 8140823040 003c40"
               "00814340 00804c40 00ff2f00");
}

Bytes smfExampleFormat1()
{
    return hex("4d546864 00000006 0001 0004 0060"
               "4d54726b 00000014 00ff580404021808 00ff510307a120 8300ff2f00"
               "4d54726b 00000010 00c005 8140904c20 81404c00 00ff2f00"
               "4d54726b 0000000f 00c12e 60914340 82204300 00ff2f00"
               "4d54726b 00000015 00c246 00923060 003c60 83003000 003c00 00ff2f00");
}

Bytes smfExampleFormat1MergedToType0()
{
    return hex("4d546864 00000006 0000 0001 0060 4d54726b 0000003a"
               "00ff580404021808 00ff510307a120 00c005 00c12e 00c246 00923060"
               "003c60 60914340 60904c20 81404c00 00914300 00923000 003c00"
               "00ff2f00");
}

// Toolbox's Golden/Core/MidiFiles/synthetic-type1.mid (480 PPQ, two tracks).
Bytes syntheticType1()
{
    return hex("4d546864000000060001000201e04d54726b0000002c00ff03044d65746100ff"
               "510307a12000ff5804030218088740ff5103061a8000ff5804070318088360ff"
               "2f004d54726b0000004f00ff03054e6f74657300903c6400405a00430183603c"
               "000080404000903c7f00b0407f78e0006000c00500d02100a03c148170f0037e"
               "01f778803c00004300009048500180480000b0400000ff2f00");
}

Bytes syntheticType0Smpte()
{
    return hex("4d5468640000000600000001e7284d54726b0000001400913064288130000091"
               "32645091320000ff2f00");
}

Bytes syntheticType0SmpteRewritten()
{
    return hex("4d5468640000000600000001e7284d54726b0000001300913064288130000091"
               "326450320000ff2f00");
}

Bytes exportLikeType1()
{
    return hex("4d546864000000060001000203c04d54726b0000002a00ff0309506174746572"
               "6e203100ff510307270e00ff5804040201609e00ff5804070301609a20ff2f00"
               "4d54726b0000008f00ff030a436166c3a9204b69636b0099245a002a4078892a"
               "007824000099245b002b4078892b007824000099245c002a4078892a00782400"
               "0099245d002b4078892b007824000099245e002a4028903c6400b04a2150892a"
               "007824000099245f002b4078892b0078240000992460002a4078892a00782400"
               "00992461002b4078892b0078240028803c40a878ff2f00");
}

Bytes exportLikeType0Merged()
{
    return hex("4d546864000000060000000103c04d54726b000000b400ff0309506174746572"
               "6e203100ff510307270e00ff58040402016000ff030a436166c3a9204b69636b"
               "0099245a002a4078892a007824000099245b002b4078892b007824000099245c"
               "002a4078892a007824000099245d002b4078892b007824000099245e002a4028"
               "903c6400b04a2150892a007824000099245f002b4078892b0078240000992460"
               "002a4078892a0078240000992461002b4078892b0078240028803c408e58ff58"
               "04070301609a20ff2f00");
}

// The export-like file's events, in the order the writer is given them, with
// two fractional times (1000.5 rounds to 1000, ties to even; 1960.25 to 1960).
File exportLikeFile()
{
    auto meta = trackOf({at(0, hex("ff0309 5061747465726e2031")),
                         at(0, hex("ff510307270e")),
                         at(0, hex("ff580404020160")),
                         at(3840, hex("ff580407030160")),
                         at(7200, hex("ff2f00"))});

    auto notes = Track {};
    notes.events.push_back(at(0, hex("ff030a 436166c3a9204b69636b")));

    for (int step = 0; step < 8; ++step)
    {
        auto tick = step * 240.0;
        auto hat = (std::uint8_t) (0x2a + step % 2);

        notes.events.push_back(at(tick, {0x99, 0x24, (std::uint8_t) (0x5a + step)}));
        notes.events.push_back(at(tick, {0x99, hat, 0x40}));

        if (step == 4)
        {
            notes.events.push_back(at(1000.5, hex("903c64")));
            notes.events.push_back(at(1000.5, hex("b04a21")));
        }

        notes.events.push_back(at(tick + 120.0, {0x89, hat, 0x00}));
        notes.events.push_back(at(tick + 240.0, {0x89, 0x24, 0x00}));
    }

    notes.events.push_back(at(1960.25, hex("803c40")));
    notes.events.push_back(at(7200, hex("ff2f00")));

    return fileOf(960, {meta, notes});
}

// The tempo map loop of the specification, written out: 120 bpm until the
// first tempo, both multiply-adds fused.
double expectedSeconds(double ticks,
                       int ticksPerQuarter,
                       const std::vector<std::pair<double, int>>& breakpoints)
{
    auto tickLength = 1.0 / ticksPerQuarter;
    auto secondsPerTick = 0.5 * tickLength;
    auto accumulated = 0.0;
    auto lastTick = 0.0;

    for (auto& [tick, microseconds]: breakpoints)
    {
        if (tick >= ticks)
            break;

        accumulated = std::fma(tick - lastTick, secondsPerTick, accumulated);
        lastTick = tick;

        if (microseconds > 0)
            secondsPerTick = tickLength * (microseconds / 1000000.0);
    }

    return std::fma(ticks - lastTick, secondsPerTick, accumulated);
}

Bytes oneEventFile(int timeFormat, double time, const Bytes& event)
{
    return fileOf(timeFormat, {trackOf({at(time, event)})}).toBytes(1);
}

// ---------------------------------------------------------------------------
// Variable-length quantities
// ---------------------------------------------------------------------------

const std::pair<std::uint32_t, const char*> vlqTable[] = {
    {0x00000000, "00"},
    {0x00000040, "40"},
    {0x0000007f, "7f"},
    {0x00000080, "8100"},
    {0x00002000, "c000"},
    {0x00003fff, "ff7f"},
    {0x00004000, "818000"},
    {0x00100000, "c08000"},
    {0x001fffff, "ffff7f"},
    {0x00200000, "81808000"},
    {0x08000000, "c0808000"},
    {0x0fffffff, "ffffff7f"},
};

auto tVlqWrite = test("MidiFile/Vlq/writesTheSpecificationTable") = []
{
    for (auto& [value, encoded]: vlqTable)
    {
        auto expected =
            concat({header(1, 1, 96),
                    trackChunk(concat({hex(encoded), hex("903c40 00ff2f00")}))});

        check(oneEventFile(96, (double) value, hex("903c40")) == expected);
    }
};

auto tVlqRead = test("MidiFile/Vlq/readsTheSpecificationTable") = []
{
    for (auto& [value, encoded]: vlqTable)
    {
        auto bytes = concat(
            {header(1, 1, 96), trackChunk(concat({hex(encoded), hex("903c40")}))});
        auto file = readOrFail(bytes);

        check(file.tracks.size() == 1);
        check(eventIs(file.tracks[0], 0, (double) value, hex("903c40")));
    }
};

auto tVlqNonMinimal = test("MidiFile/Vlq/readsNonMinimalEncodings") = []
{
    auto file = readOrFail(
        concat({header(1, 1, 96), trackChunk(hex("8000 903c40 818000 3e40"))}));

    check(eventIs(file.tracks[0], 0, 0, hex("903c40")));
    check(eventIs(file.tracks[0], 1, 0x4000, hex("903e40")));
};

auto tVlqTooLong = test("MidiFile/Vlq/aFiveByteDeltaEndsTheTrack") = []
{
    // The fourth byte still has its top bit set: malformed, the track ends with
    // what came before it.
    auto file = readOrFail(concat(
        {header(1, 1, 96), trackChunk(hex("00903c40 ffffffff7f 903e40 00ff2f00"))}));

    check(file.tracks.size() == 1);
    check(file.tracks[0].events.size() == 1);
    check(eventIs(file.tracks[0], 0, 0, hex("903c40")));
};

auto tVlqCut = test("MidiFile/Vlq/aDeltaCutByTheChunkEndEndsTheTrack") = []
{
    auto file =
        readOrFail(concat({header(1, 1, 96), trackChunk(hex("00903c40 8180"))}));

    check(file.tracks[0].events.size() == 1);
};

auto tVlqDeltaWithoutEvent = test("MidiFile/Vlq/aDeltaWithNoEventIsDropped") = []
{
    auto file =
        readOrFail(concat({header(1, 1, 96), trackChunk(hex("00903c40 10"))}));

    check(file.tracks[0].events.size() == 1);
};

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

auto tHeaderFields = test("MidiFile/Header/readsFormatTracksAndDivision") = []
{
    auto file = readOrFail(concat({header(2, 2, 0x01e0),
                                   trackChunk(hex("00ff2f00")),
                                   trackChunk(hex("00ff2f00"))}));

    check(file.fileType == 2);
    check(file.timeFormat == 480);
    check(file.tracks.size() == 2);
};

auto tHeaderSigned = test("MidiFile/Header/divisionIsASigned16BitValue") = []
{
    check(readOrFail(concat({header(0, 1, 0xe728), trackChunk(hex("00ff2f00"))}))
              .timeFormat
          == -6360);
    check(readOrFail(concat({header(0, 1, 0x7fff), trackChunk(hex("00ff2f00"))}))
              .timeFormat
          == 32767);
    check(readOrFail(concat({header(0, 1, 0xe250), trackChunk(hex("00ff2f00"))}))
              .timeFormat
          == -7600);
};

auto tHeaderRejects = test("MidiFile/Header/rejectsUnusableHeaders") = []
{
    auto valid = concat({header(1, 1, 96), trackChunk(hex("00ff2f00"))});
    auto reject = [](const Bytes& bytes)
    {
        auto file = File {};
        return !file.readFrom(view(bytes));
    };

    check(reject({}));
    check(reject(Bytes(valid.begin(), valid.begin() + 13)));

    auto badMagic = valid;
    badMagic[3] = 'e';
    check(reject(badMagic));

    auto riff = concat({hex("52494646 00000000 524d4944"), valid});
    check(reject(riff));

    auto shortLength = valid;
    shortLength[7] = 5;
    check(reject(shortLength));

    auto lengthPastEnd = valid;
    lengthPastEnd[6] = 0x10;
    check(reject(lengthPastEnd));

    check(reject(concat({header(3, 1, 96), trackChunk(hex("00ff2f00"))})));
    check(reject(concat({header(1, 1, 0), trackChunk(hex("00ff2f00"))})));
};

auto tHeaderLonger = test("MidiFile/Header/skipsExtraHeaderBytes") = []
{
    auto bytes = concat({hex("4d546864 00000008 0001 0001 0060 abcd"),
                         trackChunk(hex("00903c40 00ff2f00"))});
    auto file = readOrFail(bytes);

    check(file.timeFormat == 96);
    check(file.tracks.size() == 1);
    check(eventIs(file.tracks[0], 0, 0, hex("903c40")));
};

auto tHeaderUntouched = test("MidiFile/Header/aFailedReadLeavesTheFileAlone") = []
{
    auto file = fileOf(123, {trackOf({at(5, hex("903c40"))})});
    file.fileType = 2;
    auto before = file;

    check(!file.readFrom(view(hex("4d546864 00000006 0003 0001 0060"))));
    check(file == before);
};

auto tHeaderZeroTracks = test("MidiFile/Header/zeroTracksReadsNoTrack") = []
{
    auto file =
        readOrFail(concat({header(1, 0, 96), trackChunk(hex("00903c40 00ff2f00"))}));

    check(file.tracks.empty());
    check(file.fileType == 1);
};

auto tHeaderReplaces = test("MidiFile/Header/aReadReplacesEverything") = []
{
    auto file = fileOf(960, {trackOf({at(5, hex("903c40"))}), Track {}});
    auto bytes = concat({header(0, 1, 96), trackChunk(hex("00ff2f00"))});

    check(file.readFrom(view(bytes)));
    check(file.timeFormat == 96);
    check(file.fileType == 0);
    check(file.tracks.size() == 1);
    check(eventIs(file.tracks[0], 0, 0, hex("ff2f00")));
};

// ---------------------------------------------------------------------------
// The SMF 1.0 specification's examples
// ---------------------------------------------------------------------------

auto tSmfFormat0Read = test("MidiFile/SmfExamples/readsFormat0") = []
{
    auto file = readOrFail(smfExampleFormat0());

    check(file.fileType == 0);
    check(file.timeFormat == 96);
    check(file.tracks.size() == 1);

    auto& track = file.tracks[0];
    check(track.events.size() == 14);
    check(eventIs(track, 0, 0, hex("ff580404021808")));
    check(eventIs(track, 1, 0, hex("ff510307a120")));
    check(eventIs(track, 2, 0, hex("c005")));
    check(eventIs(track, 3, 0, hex("c12e")));
    check(eventIs(track, 4, 0, hex("c246")));
    check(eventIs(track, 5, 0, hex("923060")));
    check(eventIs(track, 6, 0, hex("923c60")));
    check(eventIs(track, 7, 96, hex("914340")));
    check(eventIs(track, 8, 192, hex("904c20")));
    check(eventIs(track, 9, 384, hex("823040")));
    check(eventIs(track, 10, 384, hex("823c40")));
    check(eventIs(track, 11, 384, hex("814340")));
    check(eventIs(track, 12, 384, hex("804c40")));
    check(eventIs(track, 13, 384, hex("ff2f00")));
};

auto tSmfFormat0Write = test("MidiFile/SmfExamples/rewritesFormat0Exactly") = []
{ check(readOrFail(smfExampleFormat0()).toBytes(0) == smfExampleFormat0()); };

auto tSmfFormat1Read = test("MidiFile/SmfExamples/readsFormat1") = []
{
    auto file = readOrFail(smfExampleFormat1());

    check(file.fileType == 1);
    check(file.timeFormat == 96);
    check(file.tracks.size() == 4);

    check(file.tracks[0].events.size() == 3);
    check(eventIs(file.tracks[0], 2, 384, hex("ff2f00")));

    check(file.tracks[1].events.size() == 4);
    check(eventIs(file.tracks[1], 0, 0, hex("c005")));
    check(eventIs(file.tracks[1], 1, 192, hex("904c20")));
    check(eventIs(file.tracks[1], 2, 384, hex("904c00")));
    check(eventIs(file.tracks[1], 3, 384, hex("ff2f00")));

    check(eventIs(file.tracks[2], 1, 96, hex("914340")));
    check(eventIs(file.tracks[2], 2, 384, hex("914300")));

    check(file.tracks[3].events.size() == 6);
    check(eventIs(file.tracks[3], 2, 0, hex("923c60")));
    check(eventIs(file.tracks[3], 3, 384, hex("923000")));
    check(eventIs(file.tracks[3], 4, 384, hex("923c00")));
};

auto tSmfFormat1Write = test("MidiFile/SmfExamples/rewritesFormat1Exactly") = []
{ check(readOrFail(smfExampleFormat1()).toBytes(1) == smfExampleFormat1()); };

auto tSmfFormat1Merge = test("MidiFile/SmfExamples/mergesFormat1IntoType0") = []
{
    check(readOrFail(smfExampleFormat1()).toBytes(0)
          == smfExampleFormat1MergedToType0());
};

auto tSmfSysExPackets = test("MidiFile/SmfExamples/sysExSentAsPackets") = []
{
    // The specification's example of a dump split into timed packets: an F0
    // event without a closing F7, then F7 continuations, the last one closing.
    auto data = hex("00 f0 03 431200"
                    "8148 f7 06 431200431200"
                    "64 f7 06 431200431200"
                    "64 f7 04 431200f7"
                    "00 ff2f00");
    auto bytes = concat({header(0, 1, 96), trackChunk(data)});
    auto file = readOrFail(bytes);
    auto& track = file.tracks[0];

    check(track.events.size() == 5);
    check(eventIs(track, 0, 0, hex("f0431200")));
    check(eventIs(track, 1, 200, hex("f7431200431200")));
    check(eventIs(track, 2, 300, hex("f7431200431200")));
    check(eventIs(track, 3, 400, hex("f7431200f7")));
    check(track.events[0].isSysEx());
    check(track.events[1].isSysEx());
    check(file.toBytes(0) == bytes);
};

// ---------------------------------------------------------------------------
// Running status
// ---------------------------------------------------------------------------

auto tRunningRead =
    test("MidiFile/RunningStatus/readAppliesTheLastChannelStatus") = []
{
    auto file = readOrFail(concat(
        {header(1, 1, 96),
         trackChunk(hex("00903c40 103e40 10803c00 003e00 00c005 0006 00ff2f00"))}));
    auto& track = file.tracks[0];

    check(track.events.size() == 7);
    check(eventIs(track, 0, 0, hex("903c40")));
    check(eventIs(track, 1, 16, hex("903e40")));
    check(eventIs(track, 2, 32, hex("803c00")));
    check(eventIs(track, 3, 32, hex("803e00")));
    check(eventIs(track, 4, 32, hex("c005")));
    check(eventIs(track, 5, 32, hex("c006")));
    check(eventIs(track, 6, 32, hex("ff2f00")));
};

auto tRunningNone =
    test("MidiFile/RunningStatus/aDataByteFirstEndsOnlyThatTrack") = []
{
    auto file = readOrFail(concat({header(1, 2, 96),
                                   trackChunk(hex("003c40 00ff2f00")),
                                   trackChunk(hex("00903c40 00ff2f00"))}));

    check(file.tracks.size() == 2);
    check(file.tracks[0].events.empty());
    check(file.tracks[1].events.size() == 2);
};

auto tRunningAcrossMeta =
    test("MidiFile/RunningStatus/readKeepsTheStatusAcrossMetaAndSysEx") = []
{
    // Not conforming (meta and SysEx cancel running status), accepted: the data
    // byte runs on from the last channel status.
    auto file = readOrFail(concat(
        {header(1, 1, 96),
         trackChunk(hex("00903c40 00ff010141 003e40 00f0027ef7 004040 00ff2f00"))}));
    auto& track = file.tracks[0];

    check(track.events.size() == 6);
    check(eventIs(track, 1, 0, hex("ff010141")));
    check(eventIs(track, 2, 0, hex("903e40")));
    check(eventIs(track, 3, 0, hex("f07ef7")));
    check(eventIs(track, 4, 0, hex("904040")));
};

auto tRunningSystem = test("MidiFile/RunningStatus/systemMessagesReadAndKeepIt") = []
{
    auto file = readOrFail(
        concat({header(1, 1, 96),
                trackChunk(hex("00b00764 00f21020 00f8 00f6 000750 00f131 00f305"
                               "00ff2f00"))}));
    auto& track = file.tracks[0];

    check(track.events.size() == 8);
    check(eventIs(track, 0, 0, hex("b00764")));
    check(eventIs(track, 1, 0, hex("f21020")));
    check(eventIs(track, 2, 0, hex("f8")));
    check(eventIs(track, 3, 0, hex("f6")));
    check(eventIs(track, 4, 0, hex("b00750")));
    check(eventIs(track, 5, 0, hex("f131")));
    check(eventIs(track, 6, 0, hex("f305")));
};

auto tRunningWrite = test("MidiFile/RunningStatus/writeOmitsARepeatedStatus") = []
{
    auto file = fileOf(96,
                       {trackOf({at(0, hex("903c40")),
                                 at(0, hex("903e40")),
                                 at(10, hex("903c00")),
                                 at(10, hex("803e00")),
                                 at(10, hex("c005")),
                                 at(10, hex("c006"))})});

    check(file.toBytes(1)
          == concat({header(1, 1, 96),
                     trackChunk(hex("00903c40 003e40 0a3c00 00803e00 00c005 0006"
                                    "00ff2f00"))}));
};

auto tRunningChannels =
    test("MidiFile/RunningStatus/writeNeverRunsAcrossChannels") = []
{
    auto file = fileOf(96, {trackOf({at(0, hex("903c40")), at(0, hex("913c40"))})});

    check(file.toBytes(1)
          == concat(
              {header(1, 1, 96), trackChunk(hex("00903c40 00913c40 00ff2f00"))}));
};

auto tRunningMetaWrite =
    test("MidiFile/RunningStatus/metaAndSysExCancelItOnWrite") = []
{
    auto file = fileOf(96,
                       {trackOf({at(0, hex("903c40")),
                                 at(0, hex("ff010141")),
                                 at(0, hex("903e40")),
                                 at(0, hex("f07ef7")),
                                 at(0, hex("904040")),
                                 at(0, hex("f8")),
                                 at(0, hex("904240"))})});

    check(file.toBytes(1)
          == concat({header(1, 1, 96),
                     trackChunk(hex("00903c40 00ff010141 00903e40 00f0027ef7"
                                    "00904040 00f8 00904240 00ff2f00"))}));
};

// ---------------------------------------------------------------------------
// SysEx
// ---------------------------------------------------------------------------

auto tSysExRead = test("MidiFile/SysEx/readStoresTheBytesAfterTheLength") = []
{
    auto file = readOrFail(concat({header(1, 1, 96),
                                   trackChunk(hex("00f005411042 12f7 00f000"
                                                  "00ff2f00"))}));
    auto& track = file.tracks[0];

    check(eventIs(track, 0, 0, hex("f041104212f7")));
    check(eventIs(track, 1, 0, hex("f0")));
};

auto tSysExWrite = test("MidiFile/SysEx/writeInsertsTheLength") = []
{
    auto file = fileOf(
        96,
        {trackOf(
            {at(0, hex("f041104212f7")), at(4, hex("f7f8")), at(4, hex("f0"))})});

    check(file.toBytes(1)
          == concat({header(1, 1, 96),
                     trackChunk(hex("00f00541104212f7 04f701f8 00f000 00ff2f00"))}));
};

auto tSysExLong = test("MidiFile/SysEx/longDumpsHaveNoSizeCap") = []
{
    auto dump = Bytes {0xf0};

    for (int i = 0; i < 298; ++i)
        dump.push_back((std::uint8_t) (i & 0x7f));

    dump.push_back(0xf7);

    auto file = fileOf(96, {trackOf({at(0, dump), at(10, hex("ff2f00"))})});
    auto bytes = file.toBytes(1);
    auto payload = Bytes(dump.begin() + 1, dump.end());

    check(bytes
          == concat(
              {header(1, 1, 96),
               trackChunk(concat({hex("00f0 822b"), payload, hex("0aff2f00")}))}));

    auto read = readOrFail(bytes);
    check(read.tracks == file.tracks);
};

// ---------------------------------------------------------------------------
// Meta events and the message helpers
// ---------------------------------------------------------------------------

auto tMetaRead = test("MidiFile/Meta/readKeepsTheEventVerbatim") = []
{
    auto file = readOrFail(
        concat({header(1, 1, 96),
                trackChunk(hex("00ff03054e6f746573 00ff7f03000102 00ff2f00"))}));
    auto& track = file.tracks[0];

    check(eventIs(track, 0, 0, hex("ff03054e6f746573")));
    check(eventIs(track, 1, 0, hex("ff7f03000102")));
    check(track.events[0].isMeta());
    check(track.events[0].metaType() == 3);
    check(track.events[1].metaType() == 0x7f);
};

auto tMetaLong = test("MidiFile/Meta/aLongMetaUsesAMultiByteLength") = []
{
    auto text = Bytes(200, 'a');
    auto message = TimedMessage::meta(7, 1, view(text));

    check(message.time == 7);
    check(message.bytes == concat({hex("ff018148"), text}));

    auto file = fileOf(96, {trackOf({message, TimedMessage::endOfTrack(7)})});
    auto bytes = file.toBytes(1);

    check(
        bytes
        == concat({header(1, 1, 96),
                   trackChunk(concat({hex("07ff018148"), text, hex("00ff2f00")}))}));
    check(readOrFail(bytes).tracks == file.tracks);
};

auto tMetaBuilders = test("MidiFile/Meta/buildersMakeTheStoredForm") = []
{
    auto hi = hex("4869");

    check(TimedMessage::endOfTrack(3).bytes == hex("ff2f00"));
    check(TimedMessage::endOfTrack(3).time == 3);
    check(TimedMessage::tempo(0, 500000).bytes == hex("ff510307a120"));
    check(TimedMessage::tempo(0, 468750).bytes == hex("ff510307270e"));
    check(TimedMessage::timeSignature(0, 4, 4).bytes == hex("ff580404021808"));
    check(TimedMessage::timeSignature(0, 6, 8, 36, 8).bytes
          == hex("ff580406032408"));
    check(TimedMessage::timeSignature(0, 7, 8, 1, 96).bytes
          == hex("ff580407030160"));
    check(TimedMessage::meta(0, 3, view(hi)).bytes == hex("ff03024869"));
    check(TimedMessage::meta(0, 0x2f).bytes == hex("ff2f00"));
};

auto tMetaQueries = test("MidiFile/Meta/queriesReadTheStoredForm") = []
{
    auto tempo = at(0, hex("ff510307a120"));
    check(tempo.isTempo());
    check(tempo.microsecondsPerQuarterNote() == 500000);
    check(!tempo.isTimeSignature());
    check(!tempo.isEndOfTrack());

    auto signature = at(0, hex("ff580406032408"));
    check(signature.isTimeSignature());
    check(signature.timeSignatureNumerator() == 6);
    check(signature.timeSignatureDenominator() == 8);
    check(signature.microsecondsPerQuarterNote() == 0);

    check(!at(0, hex("ff510207a1")).isTempo());
    check(at(0, hex("ff510207a1")).isMeta());
    check(!at(0, hex("ff5803040218")).isTimeSignature());
    check(at(0, hex("ff2f00")).isEndOfTrack());
    check(at(0, hex("ff2f")).isEndOfTrack());

    auto note = at(0, hex("903c40"));
    check(!note.isMeta());
    check(note.metaType() == -1);
    check(!note.isSysEx());
    check(note.timeSignatureNumerator() == 0);
    check(note.timeSignatureDenominator() == 0);

    check(at(0, hex("f07ef7")).isSysEx());
    check(at(0, hex("f7f8")).isSysEx());

    auto empty = TimedMessage {};
    check(!empty.isMeta());
    check(empty.metaType() == -1);
    check(!empty.isSysEx());
    check(!empty.isTempo());
    check(!empty.isEndOfTrack());
    check(!at(0, hex("ff")).isMeta());
};

// ---------------------------------------------------------------------------
// End of track
// ---------------------------------------------------------------------------

auto tEotRead = test("MidiFile/EndOfTrack/readStopsAtTheFirstOne") = []
{
    auto file = readOrFail(concat(
        {header(1, 1, 96), trackChunk(hex("00903c40 05ff2f00 00903e40 00ff2f00"))}));
    auto& track = file.tracks[0];

    check(track.events.size() == 2);
    check(eventIs(track, 1, 5, hex("ff2f00")));
};

auto tEotMissingRead = test("MidiFile/EndOfTrack/aTrackWithoutOneReadsAsIs") = []
{
    auto file =
        readOrFail(concat({header(1, 1, 96), trackChunk(hex("00903c40 10803c00"))}));

    check(file.tracks[0].events.size() == 2);
    check(!file.tracks[0].events[1].isEndOfTrack());
};

auto tEotAppended = test("MidiFile/EndOfTrack/writeAppendsOneWhenMissing") = []
{
    auto file =
        fileOf(480, {trackOf({at(0, hex("903c40")), at(480, hex("803c00"))})});

    check(file.toBytes(1)
          == concat(
              {header(1, 1, 480), trackChunk(hex("00903c40 8360803c00 00ff2f00"))}));
};

auto tEotKept = test("MidiFile/EndOfTrack/writeKeepsOneWhereItStands") = []
{
    auto file = fileOf(
        96,
        {trackOf(
            {at(0, hex("903c40")), at(10, hex("ff2f00")), at(20, hex("803c00"))})});

    check(file.toBytes(1)
          == concat(
              {header(1, 1, 96), trackChunk(hex("00903c40 0aff2f00 0a803c00"))}));
};

auto tEotEmpty = test("MidiFile/EndOfTrack/anEmptyTrackIsOnlyAnEndOfTrack") = []
{
    auto file = fileOf(96, {Track {}});

    check(file.toBytes(1)
          == concat({header(1, 1, 96), hex("4d54726b 00000004 00ff2f00")}));
};

// ---------------------------------------------------------------------------
// Writing: times, order, failures
// ---------------------------------------------------------------------------

auto tWriteRounding = test("MidiFile/Write/timesRoundToNearestTiesToEven") = []
{
    auto file = fileOf(96,
                       {trackOf({at(2.5, hex("903c40")),
                                 at(3.5, hex("903c40")),
                                 at(4.4999, hex("903c40")),
                                 at(5.5000001, hex("903c40")),
                                 at(1000.5, hex("903c40")),
                                 at(1001.5, hex("903c40"))})});

    // Ticks 2, 4, 4, 6, 1000, 1002.
    check(file.toBytes(1)
          == concat({header(1, 1, 96),
                     trackChunk(hex("02903c40 023c40 003c40 023c40 8762 3c40"
                                    "02 3c40 00ff2f00"))}));
};

auto tWriteUnsorted = test("MidiFile/Write/eventsAreWrittenInTheOrderGiven") = []
{
    auto file = fileOf(96,
                       {trackOf({at(100, hex("903c40")),
                                 at(50, hex("903e40")),
                                 at(80, hex("904040"))})});
    auto bytes = file.toBytes(1);

    check(bytes
          == concat({header(1, 1, 96),
                     trackChunk(hex("64903c40 003e40 1e4040 00ff2f00"))}));

    auto read = readOrFail(bytes);
    check(eventIs(read.tracks[0], 0, 100, hex("903c40")));
    check(eventIs(read.tracks[0], 1, 100, hex("903e40")));
    check(eventIs(read.tracks[0], 2, 130, hex("904040")));
};

auto tWriteNegative = test("MidiFile/Write/aNegativeTimeWritesADeltaOfZero") = []
{
    auto file = fileOf(96, {trackOf({at(-3, hex("903c40")), at(2, hex("803c00"))})});

    check(file.toBytes(1)
          == concat(
              {header(1, 1, 96), trackChunk(hex("00903c40 05803c00 00ff2f00"))}));
};

auto tWriteFailures = test("MidiFile/Write/rejectsWhatCannotBeWritten") = []
{
    auto fails = [](const File& file, int type)
    {
        auto out = Bytes {1, 2, 3};
        auto ok = file.writeTo(out, type);
        return !ok && out == Bytes {1, 2, 3} && file.toBytes(type).empty();
    };

    auto file = fileOf(96, {Track {}});
    check(fails(file, 3));
    check(fails(file, -1));
    check(fails(fileOf(0, {Track {}}), 1));
    check(fails(fileOf(32768, {Track {}}), 1));
    check(fails(fileOf(-32769, {Track {}}), 1));

    auto many = File {};
    many.tracks.resize(65536);
    check(fails(many, 1));
};

auto tWriteAppends = test("MidiFile/Write/writeToAppends") = []
{
    auto file = fileOf(96, {trackOf({at(0, hex("903c40"))})});
    auto out = Bytes {0xaa, 0xbb};

    check(file.writeTo(out, 1));
    check(out == concat({hex("aabb"), file.toBytes(1)}));
};

auto tWriteDivision = test("MidiFile/Write/theDivisionIsWrittenAs16Bits") = []
{
    check(fileOf(960, {}).toBytes(1) == header(1, 0, 0x03c0));
    check(fileOf(-6360, {}).toBytes(1) == header(1, 0, 0xe728));
    check(fileOf(smpteTimeFormat(30, 80), {}).toBytes(2) == header(2, 0, 0xe250));
};

auto tWriteType2 = test("MidiFile/Write/type2KeepsTracksApart") = []
{
    auto file = fileOf(96,
                       {trackOf({at(0, hex("903c40")), at(10, hex("ff2f00"))}),
                        trackOf({at(5, hex("913c40")), at(20, hex("ff2f00"))})});
    auto bytes = file.toBytes(2);

    check(bytes
          == concat({header(2, 2, 96),
                     trackChunk(hex("00903c40 0aff2f00")),
                     trackChunk(hex("05913c40 0fff2f00"))}));

    auto read = readOrFail(bytes);
    check(read.fileType == 2);
    check(read.tracks == file.tracks);
};

// ---------------------------------------------------------------------------
// Type 0
// ---------------------------------------------------------------------------

auto tType0Single = test("MidiFile/Type0/aSingleTrackIsWrittenAsGiven") = []
{
    auto file =
        fileOf(96, {trackOf({at(100, hex("903c40")), at(50, hex("803c00"))})});

    check(file.toBytes(0)
          == concat(
              {header(0, 1, 96), trackChunk(hex("64903c40 00803c00 00ff2f00"))}));
};

auto tType0Merge = test("MidiFile/Type0/mergesByTimeKeepingTrackOrderOnTies") = []
{
    auto file = fileOf(96,
                       {trackOf({at(0, hex("903c40")),
                                 at(10, hex("ff010141")),
                                 at(10, hex("803c00")),
                                 at(30, hex("ff2f00"))}),
                        trackOf({at(0, hex("914040")),
                                 at(10, hex("914140")),
                                 at(10, hex("814000")),
                                 at(20, hex("ff2f00"))})});

    check(file.toBytes(0)
          == concat({header(0, 1, 96),
                     trackChunk(hex("00903c40 00914040 0aff010141 00803c00 00914140"
                                    "00814000 14ff2f00"))}));
};

auto tType0Fractional = test("MidiFile/Type0/mergeSortsTheTimesAsGiven") = []
{
    // 10.5 sorts after 10 although both are written at tick 10; equal times keep
    // the lower track first.
    auto file = fileOf(96,
                       {trackOf({at(10.5, hex("903c40")), at(12, hex("903e40"))}),
                        trackOf({at(10, hex("913c40")), at(12, hex("913e40"))})});

    check(
        file.toBytes(0)
        == concat({header(0, 1, 96),
                   trackChunk(hex("0a913c40 00903c40 023e40 00913e40 00ff2f00"))}));
};

auto tType0EndTime = test("MidiFile/Type0/theMergedEndIsTheLatestTime") = []
{
    auto shortEnd = fileOf(96,
                           {trackOf({at(0, hex("903c40")), at(50, hex("ff2f00"))}),
                            trackOf({at(100, hex("913c40"))})});

    check(shortEnd.toBytes(0)
          == concat(
              {header(0, 1, 96), trackChunk(hex("00903c40 64913c40 00ff2f00"))}));

    auto longEnd = fileOf(96,
                          {trackOf({at(0, hex("903c40")), at(500, hex("ff2f00"))}),
                           trackOf({at(100, hex("913c40"))})});

    check(longEnd.toBytes(0)
          == concat(
              {header(0, 1, 96), trackChunk(hex("00903c40 64913c40 8310ff2f00"))}));
};

auto tType0Empty = test("MidiFile/Type0/zeroTracksWriteOneEmptyTrack") = []
{
    check(fileOf(96, {}).toBytes(0)
          == concat({header(0, 1, 96), hex("4d54726b 00000004 00ff2f00")}));
    check(fileOf(96, {}).toBytes(1) == header(1, 0, 96));
};

auto tType0FromTwoEmpty = test("MidiFile/Type0/twoEmptyTracksMergeToOne") = []
{
    check(fileOf(96, {Track {}, Track {}}).toBytes(0)
          == concat({header(0, 1, 96), hex("4d54726b 00000004 00ff2f00")}));
};

// ---------------------------------------------------------------------------
// SMPTE divisions
// ---------------------------------------------------------------------------

auto tSmpteFormat = test("MidiFile/Smpte/timeFormatFromFramesAndTicks") = []
{
    check(smpteTimeFormat(25, 40) == -6360);
    check(smpteTimeFormat(30, 80) == -7600);
    check(smpteTimeFormat(24, 4) == -6140);
    check(smpteTimeFormat(29, 100) == -7324);
};

auto tSmpteSeconds = test("MidiFile/Smpte/secondsAreTicksOverFramesTimesTicks") = []
{
    auto file = fileOf(-6360, {trackOf({at(0, hex("ff510307a120"))})});

    check(bitsAre(file.ticksToSeconds(40), 0x3fa47ae147ae147bull));
    check(bitsAre(file.ticksToSeconds(120), 0x3fbeb851eb851eb8ull));
    check(file.ticksToSeconds(1000) == 1.0);
    check(file.ticksToSeconds(40) == 40.0 / 1000.0);

    check(fileOf(smpteTimeFormat(30, 80), {}).ticksToSeconds(2400) == 1.0);
    check(fileOf(smpteTimeFormat(24, 4), {}).ticksToSeconds(48) == 0.5);

    // Code 29 is used as 29 frames per second, not 29.97.
    check(fileOf(smpteTimeFormat(29, 100), {}).ticksToSeconds(2900) == 1.0);
};

auto tSmpteFile = test("MidiFile/Smpte/readsAndRewritesTheSyntheticFile") = []
{
    auto file = readOrFail(syntheticType0Smpte());
    auto& track = file.tracks[0];

    check(file.timeFormat == -6360);
    check(file.fileType == 0);
    check(track.events.size() == 5);
    check(eventIs(track, 0, 0, hex("913064")));
    check(eventIs(track, 1, 40, hex("813000")));
    check(eventIs(track, 2, 40, hex("913264")));
    check(eventIs(track, 3, 120, hex("913200")));
    check(eventIs(track, 4, 120, hex("ff2f00")));
    check(file.toBytes(0) == syntheticType0SmpteRewritten());

    auto map = file.tempoMap();
    check(map.timeFormat == -6360);
    check(map.events.empty());
    check(bitsAre(map.ticksToSeconds(120), 0x3fbeb851eb851eb8ull));
};

// ---------------------------------------------------------------------------
// Tempo map
// ---------------------------------------------------------------------------

auto tTempoDefault = test("MidiFile/TempoMap/defaultsTo120Bpm") = []
{
    auto file = fileOf(480, {trackOf({at(0, hex("903c40"))})});

    check(file.ticksToSeconds(0) == 0.0);
    check(file.ticksToSeconds(480) == 0.5);
    check(file.ticksToSeconds(960) == 1.0);
    check(file.ticksToSeconds(-480) == -0.5);
    check(file.ticksToSeconds(1) == expectedSeconds(1, 480, {}));
    check(file.tempoMap().events.empty());
};

auto tTempoZero = test("MidiFile/TempoMap/aZeroTimeFormatLeavesTicksAlone") = []
{
    auto file = fileOf(0, {trackOf({at(0, hex("ff510307a120"))})});

    check(file.ticksToSeconds(123.5) == 123.5);
};

auto tTempoSynthetic = test("MidiFile/TempoMap/syntheticType1Seconds") = []
{
    auto file = readOrFail(syntheticType1());
    auto breakpoints = std::vector<std::pair<double, int>> {
        {0, 500000}, {0, -1}, {960, 400000}, {960, -1}};

    check(bitsAre(file.ticksToSeconds(0), 0));
    check(file.ticksToSeconds(480) == 0.5);
    check(file.ticksToSeconds(600) == 0.625);
    check(file.ticksToSeconds(840) == 0.875);
    check(file.ticksToSeconds(960) == 1.0);
    check(bitsAre(file.ticksToSeconds(961), 0x3ff00369d0369d03ull));
    check(bitsAre(file.ticksToSeconds(1440), 0x3ff6666666666667ull));

    // The fused multiply-add matters: rounded separately, tick 1440 lands one
    // unit lower (the product kept in its own statement so it cannot fuse).
    volatile auto product = 480.0 * ((1.0 / 480) * (400000 / 1000000.0));
    auto unfused = 1.0 + product;
    check(bitsAre(unfused, 0x3ff6666666666666ull));

    for (auto ticks: {0.0, 1.0, 480.0, 959.0, 960.0, 961.0, 1440.0, 2000.25})
        check(file.ticksToSeconds(ticks)
              == expectedSeconds(ticks, 480, breakpoints));

    auto map = file.tempoMap();
    check(map.timeFormat == 480);
    check(map.events.size() == 4);
    check(map.events[0].message == at(0, hex("ff510307a120")));
    check(map.events[1].message == at(0, hex("ff580403021808")));
    check(map.events[2].message == at(960, hex("ff5103061a80")));
    check(map.events[3].message == at(960, hex("ff580407031808")));
    check(map.events[2].seconds == 1.0);
    check(map.events[3].seconds == 1.0);
};

File multiTempoFile()
{
    return fileOf(480,
                  {trackOf({TimedMessage::tempo(0, 500000),
                            TimedMessage::timeSignature(0, 4, 4),
                            TimedMessage::tempo(480, 400000),
                            TimedMessage::timeSignature(700, 3, 4),
                            TimedMessage::endOfTrack(1700)}),
                   trackOf({TimedMessage::tempo(1000, 333333),
                            TimedMessage::tempo(1000, 600000),
                            TimedMessage::endOfTrack(1700)})});
}

auto tTempoMultiOrder =
    test("MidiFile/TempoMap/collectsTemposThenSignaturesByTick") = []
{
    auto map = multiTempoFile().tempoMap();

    check(map.events.size() == 6);
    check(map.events[0].message.time == 0 && map.events[0].message.isTempo());
    check(map.events[1].message.time == 0
          && map.events[1].message.isTimeSignature());
    check(map.events[2].message.time == 480
          && map.events[2].message.microsecondsPerQuarterNote() == 400000);
    check(map.events[3].message.time == 700
          && map.events[3].message.isTimeSignature());
    check(map.events[4].message.time == 1000
          && map.events[4].message.microsecondsPerQuarterNote() == 333333);
    check(map.events[5].message.time == 1000
          && map.events[5].message.microsecondsPerQuarterNote() == 600000);

    check(map.events[0].seconds == 0.0);
    check(map.events[2].seconds == 0.5);
    check(bitsAre(map.events[3].seconds, 0x3fe5dddddddddddeull));
    check(bitsAre(map.events[4].seconds, 0x3feddddddddddddeull));
    check(bitsAre(map.events[5].seconds, 0x3feddddddddddddeull));

    for (auto& event: map.events)
        check(event.seconds == map.ticksToSeconds(event.message.time));
};

auto tTempoMultiSeconds = test("MidiFile/TempoMap/severalTempoChanges") = []
{
    auto file = multiTempoFile();
    auto breakpoints = std::vector<std::pair<double, int>> {{0, 500000},
                                                            {0, -1},
                                                            {480, 400000},
                                                            {700, -1},
                                                            {1000, 333333},
                                                            {1000, 600000}};

    check(file.ticksToSeconds(240) == 0.25);
    check(file.ticksToSeconds(480) == 0.5);
    check(bitsAre(file.ticksToSeconds(700), 0x3fe5dddddddddddeull));
    check(bitsAre(file.ticksToSeconds(1000), 0x3feddddddddddddeull));
    check(bitsAre(file.ticksToSeconds(1001), 0x3fede81b4e81b4e8ull));
    check(bitsAre(file.ticksToSeconds(1700), 0x3ffceeeeeeeeeeefull));
    check(bitsAre(file.ticksToSeconds(2000.5), 0x400178bf258bf259ull));

    auto queries = std::vector<double> {
        0, 100, 479, 480, 481, 699, 700, 701, 999, 1000, 1001, 1700, 2000.5, 100000};

    for (auto ticks: queries)
        check(file.ticksToSeconds(ticks)
              == expectedSeconds(ticks, 480, breakpoints));
};

auto tTempoAtTick =
    test("MidiFile/TempoMap/aTempoAtTheQueriedTickAppliesAfterIt") = []
{
    auto file = fileOf(480, {trackOf({TimedMessage::tempo(480, 1000000)})});

    check(file.ticksToSeconds(480) == 0.5);
    check(file.ticksToSeconds(960) == 1.5);
};

auto tTempoMalformed =
    test("MidiFile/TempoMap/ignoresMalformedTempoAndSignatures") = []
{
    auto file = fileOf(480,
                       {trackOf({at(0, hex("ff510207a1")),
                                 at(0, hex("ff51030f")),
                                 at(0, hex("ff5803040218")),
                                 at(0, hex("ff510307a120"))})});

    check(file.tempoMap().events.size() == 1);
};

auto tTempoTrackOrder = test("MidiFile/TempoMap/temposComeFirstAtOneTick") = []
{
    auto file = fileOf(480,
                       {trackOf({TimedMessage::timeSignature(0, 3, 4),
                                 TimedMessage::tempo(0, 600000)}),
                        trackOf({TimedMessage::tempo(0, 400000),
                                 TimedMessage::timeSignature(0, 5, 4)})});
    auto map = file.tempoMap();

    check(map.events.size() == 4);
    check(map.events[0].message.microsecondsPerQuarterNote() == 600000);
    check(map.events[1].message.microsecondsPerQuarterNote() == 400000);
    check(map.events[2].message.timeSignatureNumerator() == 3);
    check(map.events[3].message.timeSignatureNumerator() == 5);

    // The last tempo at a tick wins.
    check(file.ticksToSeconds(480) == 480 * ((1.0 / 480) * 0.4));
};

auto tTempoHandBuilt = test("MidiFile/TempoMap/aHandBuiltMapIsWalkedAsStored") = []
{
    auto map = TempoMap {};
    map.timeFormat = 128;
    map.events.push_back({TimedMessage::tempo(128, 1000000), 0.0});

    check(map.ticksToSeconds(128) == 0.5);
    check(map.ticksToSeconds(256) == 1.5);
};

// ---------------------------------------------------------------------------
// Tolerances
// ---------------------------------------------------------------------------

auto tShortLastChunk =
    test("MidiFile/Tolerance/aShortLastChunkKeepsItsWholeEvents") = []
{
    auto bytes = concat({header(1, 2, 96),
                         trackChunk(hex("00903c40 00ff2f00")),
                         chunk("MTrk", hex("00903c40 10803c00 2090"), 0x20)});
    auto file = readOrFail(bytes);

    check(file.tracks.size() == 2);
    check(file.tracks[1].events.size() == 2);
    check(eventIs(file.tracks[1], 0, 0, hex("903c40")));
    check(eventIs(file.tracks[1], 1, 16, hex("803c00")));
};

auto tCutMeta = test("MidiFile/Tolerance/aMetaCutByTheEndIsDropped") = []
{
    auto file = readOrFail(
        concat({header(1, 1, 96), chunk("MTrk", hex("00903c40 00ff03054e6f"), 40)}));

    check(file.tracks[0].events.size() == 1);
};

auto tCutSysEx = test("MidiFile/Tolerance/aSysExCutByTheChunkIsDropped") = []
{
    // Inside a well-sized chunk: the SysEx's length runs past the chunk, the
    // next chunk is still read.
    auto file = readOrFail(concat({header(1, 2, 96),
                                   trackChunk(hex("00903c40 00f00a7e7f")),
                                   trackChunk(hex("00913c40 00ff2f00"))}));

    check(file.tracks.size() == 2);
    check(file.tracks[0].events.size() == 1);
    check(file.tracks[1].events.size() == 2);
};

auto tTrailing = test("MidiFile/Tolerance/bytesAfterTheLastTrackAreIgnored") = []
{
    auto clean = readOrFail(syntheticType1());
    auto withGarbage =
        concat({syntheticType1(), hex("4d54726b 00000004 00903c40 deadbeef 00")});
    auto file = readOrFail(withGarbage);

    check(file.tracks.size() == 2);
    check(file == clean);
};

auto tTail = test("MidiFile/Tolerance/anOverAllocatedTailIsIgnored") = []
{
    auto bytes = exportLikeType1();
    auto clean = readOrFail(bytes);
    bytes.resize(bytes.size() + 4096, 0xcd);

    check(readOrFail(bytes) == clean);
};

auto tFewerChunks = test("MidiFile/Tolerance/fewerChunksThanDeclared") = []
{
    auto file = readOrFail(concat({header(1, 3, 96), trackChunk(hex("00ff2f00"))}));
    check(file.tracks.size() == 1);

    auto shortTail = readOrFail(
        concat({header(1, 2, 96), trackChunk(hex("00ff2f00")), hex("4d54726b00")}));
    check(shortTail.tracks.size() == 1);
};

auto tAlienChunk = test("MidiFile/Tolerance/otherChunksAreSkippedAndNotCounted") = []
{
    auto file = readOrFail(concat({header(1, 1, 96),
                                   chunk("XFIH", hex("01020304")),
                                   trackChunk(hex("00903c40 00ff2f00"))}));

    check(file.tracks.size() == 1);
    check(eventIs(file.tracks[0], 0, 0, hex("903c40")));
};

auto tFormat0Many =
    test("MidiFile/Tolerance/aFormat0HeaderWithTwoTracksReadsBoth") = []
{
    // A format word of 0 over two track chunks, as some writers produce.
    auto bytes = exportLikeType1();
    bytes[9] = 0;
    auto file = readOrFail(bytes);

    check(file.fileType == 0);
    check(file.tracks.size() == 2);
    check(file.tracks == readOrFail(exportLikeType1()).tracks);
};

// ---------------------------------------------------------------------------
// Round trips
// ---------------------------------------------------------------------------

auto tSyntheticRead = test("MidiFile/RoundTrip/syntheticType1ReadsInFileOrder") = []
{
    auto file = readOrFail(syntheticType1());

    check(file.timeFormat == 480);
    check(file.tracks.size() == 2);
    check(file.tracks[0].events.size() == 6);
    check(eventIs(file.tracks[0], 0, 0, hex("ff03044d657461")));
    check(eventIs(file.tracks[0], 5, 1440, hex("ff2f00")));

    auto& notes = file.tracks[1];
    check(notes.events.size() == 19);
    check(eventIs(notes, 1, 0, hex("903c64")));
    check(eventIs(notes, 2, 0, hex("90405a")));
    check(eventIs(notes, 3, 0, hex("904301")));
    check(eventIs(notes, 4, 480, hex("903c00")));
    check(eventIs(notes, 5, 480, hex("804040")));
    check(eventIs(notes, 6, 480, hex("903c7f")));
    check(eventIs(notes, 7, 480, hex("b0407f")));
    check(eventIs(notes, 8, 600, hex("e00060")));
    check(eventIs(notes, 9, 600, hex("c005")));
    check(eventIs(notes, 10, 600, hex("d021")));
    check(eventIs(notes, 11, 600, hex("a03c14")));
    check(eventIs(notes, 12, 840, hex("f07e01f7")));
    check(eventIs(notes, 13, 960, hex("803c00")));
    check(eventIs(notes, 14, 960, hex("804300")));
    check(eventIs(notes, 15, 960, hex("904850")));
    check(eventIs(notes, 16, 961, hex("804800")));
    check(eventIs(notes, 17, 961, hex("b04000")));
    check(eventIs(notes, 18, 961, hex("ff2f00")));
};

auto tSyntheticBytes = test("MidiFile/RoundTrip/syntheticType1RewritesExactly") = []
{ check(readOrFail(syntheticType1()).toBytes(1) == syntheticType1()); };

auto tReadWriteRead = test("MidiFile/RoundTrip/readWriteReadGivesTheSameFile") = []
{
    for (auto& bytes: {smfExampleFormat0(),
                       smfExampleFormat1(),
                       syntheticType1(),
                       syntheticType0Smpte(),
                       exportLikeType1()})
    {
        auto first = readOrFail(bytes);
        auto second = readOrFail(first.toBytes(first.fileType));

        check(second == first);
    }
};

auto tAsType1 = test("MidiFile/RoundTrip/aType0FileRewrittenAsType1") = []
{
    auto first = readOrFail(smfExampleFormat0());
    auto second = readOrFail(first.toBytes(1));

    check(second.fileType == 1);
    check(second.tracks == first.tracks);
    check(second.timeFormat == first.timeFormat);
};

// ---------------------------------------------------------------------------
// The export-like file, from synthetic events
// ---------------------------------------------------------------------------

auto tExportType1 = test("MidiFile/ExportLike/type1BytesFromEvents") = []
{ check(exportLikeFile().toBytes(1) == exportLikeType1()); };

auto tExportType0 = test("MidiFile/ExportLike/type0MergesTheTwoTracks") = []
{ check(exportLikeFile().toBytes(0) == exportLikeType0Merged()); };

auto tExportRead = test("MidiFile/ExportLike/readsBackWithWholeTicks") = []
{
    auto expected = exportLikeFile();

    for (auto& track: expected.tracks)
        for (auto& event: track.events)
            event.time = std::nearbyint(event.time);

    auto file = readOrFail(exportLikeType1());

    check(file.timeFormat == 960);
    check(file.tracks == expected.tracks);
    check(eventIs(file.tracks[1], 19, 1000, hex("903c64")));
    check(eventIs(file.tracks[1], 35, 1960, hex("803c40")));
};
} // namespace
} // namespace MidiFileTests
