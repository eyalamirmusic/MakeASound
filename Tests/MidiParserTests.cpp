// Tests for MakeASound::MidiParser - the byte-stream state machine every native
// backend will feed raw packets into. What is worth pinning is the parts of MIDI
// 1.0 that a naive reader gets wrong: running status, realtime bytes sitting in
// the middle of another message, and a SysEx dump that arrives in pieces, runs
// past the buffer, or never ends at all.

#include <MakeASound/MIDI/MidiParser.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <vector>

using namespace nano;
using MakeASound::MidiMessageView;
using MakeASound::MidiParser;
using MakeASound::MidiTimePoint;
using MakeASound::Span;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
using Bytes = std::vector<std::uint8_t>;

MidiTimePoint at(int milliseconds)
{
    return MidiTimePoint {} + std::chrono::milliseconds {milliseconds};
}

// Records what the parser emitted so a case can assert after the fact; the
// parser itself never sees anything but the two sinks.
struct Recorder
{
    std::vector<Bytes> messages;
    std::vector<MidiTimePoint> timestamps;
    std::vector<int> dropped;

    void feed(MidiParser& parser,
              std::initializer_list<std::uint8_t> bytes,
              int milliseconds = 0)
    {
        auto buffer = Bytes(bytes);

        parser.feed(Span<const std::uint8_t>(buffer.data(), (int) buffer.size()),
                    at(milliseconds),
                    [this](const MidiMessageView& message)
                    {
                        messages.emplace_back(message.bytes.begin(),
                                              message.bytes.end());
                        timestamps.push_back(message.timestamp);
                    },
                    [this](int numBytes) { dropped.push_back(numBytes); });
    }

    bool is(int index, std::initializer_list<std::uint8_t> expected) const
    {
        return index < (int) messages.size() && messages[index] == Bytes(expected);
    }

    int count() const { return (int) messages.size(); }
};

// A parser with room for a dump of `capacity` bytes, buffer and all.
struct Fixture
{
    explicit Fixture(int capacity = 64)
        : buffer((std::size_t) capacity)
        , parser(Span<std::uint8_t>(buffer.data(), capacity))
    {
    }

    Bytes buffer;
    MidiParser parser;
    Recorder recorder;
};

// ---------------------------------------------------------------------------
// Channel messages and running status
// ---------------------------------------------------------------------------

auto tThreeByte = test("MidiParser/parsesThreeByteChannelMessages") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser,
                    {0x90, 0x3C, 0x64, 0x80, 0x3C, 0x00, 0xB0, 0x4A, 0x7F});

    check(f.recorder.count() == 3);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
    check(f.recorder.is(1, {0x80, 0x3C, 0x00}));
    check(f.recorder.is(2, {0xB0, 0x4A, 0x7F}));
};

auto tTwoByte = test("MidiParser/parsesTwoByteChannelMessages") = []
{
    // Program change and channel aftertouch carry one data byte, not two.
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0xC3, 0x0C, 0xD3, 0x40, 0x90, 0x3C, 0x64});

    check(f.recorder.count() == 3);
    check(f.recorder.is(0, {0xC3, 0x0C}));
    check(f.recorder.is(1, {0xD3, 0x40}));
    check(f.recorder.is(2, {0x90, 0x3C, 0x64}));
};

auto tRunningStatus = test("MidiParser/appliesRunningStatusToLaterDataPairs") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0x90, 0x3C, 0x64, 0x3E, 0x64, 0x40, 0x00});

    check(f.recorder.count() == 3);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
    check(f.recorder.is(1, {0x90, 0x3E, 0x64}));
    check(f.recorder.is(2, {0x90, 0x40, 0x00}));
};

auto tRunningStatusTwoByte =
    test("MidiParser/runningStatusKeepsTheShorterLengthToo") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0xC0, 0x01, 0x02, 0x03});

    check(f.recorder.count() == 3);
    check(f.recorder.is(0, {0xC0, 0x01}));
    check(f.recorder.is(1, {0xC0, 0x02}));
    check(f.recorder.is(2, {0xC0, 0x03}));
};

auto tRunningStatusClearedBySystem =
    test("MidiParser/systemCommonClearsRunningStatus") = []
{
    // 0xF6 carries no data, but per the spec it still ends running status, so
    // the two bytes after it belong to nothing.
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0x90, 0x3C, 0x64, 0xF6, 0x3E, 0x64});

    check(f.recorder.count() == 2);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
    check(f.recorder.is(1, {0xF6}));
};

auto tSplitAcrossFeeds = test("MidiParser/completesAChannelMessageAcrossFeeds") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0x90, 0x3C}, 0);
    check(f.recorder.count() == 0);

    f.recorder.feed(f.parser, {0x64}, 5);

    check(f.recorder.count() == 1);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
    check(f.recorder.timestamps[0] == at(0)); // stamped where it started
};

// ---------------------------------------------------------------------------
// System common
// ---------------------------------------------------------------------------

auto tSystemCommon = test("MidiParser/parsesSystemCommonLengths") = []
{
    // Song position is three bytes, song select and MTC quarter frame two, tune
    // request one. Getting any of these wrong eats the message after it.
    auto f = Fixture {};

    f.recorder.feed(
        f.parser, {0xF2, 0x10, 0x20, 0xF3, 0x05, 0xF1, 0x21, 0xF6, 0x90, 0x3C, 0x64});

    check(f.recorder.count() == 5);
    check(f.recorder.is(0, {0xF2, 0x10, 0x20}));
    check(f.recorder.is(1, {0xF3, 0x05}));
    check(f.recorder.is(2, {0xF1, 0x21}));
    check(f.recorder.is(3, {0xF6}));
    check(f.recorder.is(4, {0x90, 0x3C, 0x64}));
};

auto tUndefinedStatus = test("MidiParser/undefinedStatusBytesDoNotDesync") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0xF4, 0xF5, 0xF9, 0xFD, 0x90, 0x3C, 0x64});

    check(f.recorder.count() == 1);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
};

auto tStrayData = test("MidiParser/skipsDataBytesWithNoStatus") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0x40, 0x41, 0x42, 0x7F, 0x90, 0x3C, 0x64});

    check(f.recorder.count() == 1);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
};

auto tStrayEndOfSysEx = test("MidiParser/ignoresAnEndOfSysExWithNoDumpOpen") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0xF7, 0x90, 0x3C, 0x64});

    check(f.recorder.count() == 1);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
};

// ---------------------------------------------------------------------------
// Realtime
// ---------------------------------------------------------------------------

auto tRealtimeFiltered = test("MidiParser/filtersClockAndActiveSenseByDefault") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0xF8, 0xFE, 0xFA, 0xFC, 0xFF});

    // Start, stop and reset are never filtered; clock and active sense are.
    check(f.recorder.count() == 3);
    check(f.recorder.is(0, {0xFA}));
    check(f.recorder.is(1, {0xFC}));
    check(f.recorder.is(2, {0xFF}));
};

auto tRealtimeEnabled = test("MidiParser/deliversClockAndActiveSenseWhenAsked") = []
{
    auto f = Fixture {};
    f.parser.setIgnoredTypes(false, false);

    f.recorder.feed(f.parser, {0xF8, 0xFE, 0xF8});

    check(f.recorder.count() == 3);
    check(f.recorder.is(0, {0xF8}));
    check(f.recorder.is(1, {0xFE}));
    check(f.recorder.is(2, {0xF8}));
};

auto tRealtimeInsideChannelMessage =
    test("MidiParser/aRealtimeByteInsideAChannelMessageDoesNotBreakIt") = []
{
    auto f = Fixture {};
    f.parser.setIgnoredTypes(false, false);

    f.recorder.feed(f.parser, {0x90, 0x3C, 0xF8, 0x64});

    check(f.recorder.count() == 2);
    check(f.recorder.is(0, {0xF8})); // delivered the moment it arrives
    check(f.recorder.is(1, {0x90, 0x3C, 0x64}));
};

auto tRealtimeDoesNotClearRunningStatus =
    test("MidiParser/aRealtimeByteLeavesRunningStatusAlone") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0x90, 0x3C, 0x64, 0xFA, 0x3E, 0x64});

    check(f.recorder.count() == 3);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
    check(f.recorder.is(1, {0xFA}));
    check(f.recorder.is(2, {0x90, 0x3E, 0x64}));
};

// ---------------------------------------------------------------------------
// SysEx
// ---------------------------------------------------------------------------

auto tSysExWhole = test("MidiParser/deliversASysExWithItsFramingBytes") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7});

    check(f.recorder.count() == 1);
    check(f.recorder.is(0, {0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7}));
    check(f.recorder.dropped.empty());
    check(!f.parser.isInSysEx());
};

auto tSysExSplit = test("MidiParser/assemblesASysExSplitAcrossThreeFeeds") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0xF0, 0x43, 0x00}, 0);
    check(f.recorder.count() == 0);
    check(f.parser.isInSysEx());

    f.recorder.feed(f.parser, {0x01, 0x02, 0x03}, 10);
    check(f.recorder.count() == 0);

    f.recorder.feed(f.parser, {0x04, 0xF7, 0x90, 0x3C, 0x64}, 20);

    check(f.recorder.count() == 2);
    check(f.recorder.is(0, {0xF0, 0x43, 0x00, 0x01, 0x02, 0x03, 0x04, 0xF7}));
    check(f.recorder.is(1, {0x90, 0x3C, 0x64}));

    // A dump is stamped with the feed it started in, not the one it ended in.
    check(f.recorder.timestamps[0] == at(0));
    check(f.recorder.timestamps[1] == at(20));
};

auto tRealtimeInsideSysEx =
    test("MidiParser/aRealtimeByteInsideASysExPassesThroughIntact") = []
{
    auto f = Fixture {};
    f.parser.setIgnoredTypes(false, false);

    f.recorder.feed(f.parser, {0xF0, 0x7D, 0x01, 0xF8, 0x02, 0x03, 0xF7});

    check(f.recorder.count() == 2);
    check(f.recorder.is(0, {0xF8})); // delivered mid-dump
    check(f.recorder.is(1, {0xF0, 0x7D, 0x01, 0x02, 0x03, 0xF7}));
    check(f.recorder.dropped.empty());
};

auto tSysExOversize = test("MidiParser/dropsASysExThatOutgrowsTheBuffer") = []
{
    // Four bytes of room: 0xF0, two data bytes and the 0xF7 would just fit, so
    // five data bytes is one dump the buffer cannot hold.
    auto f = Fixture {4};

    f.recorder.feed(f.parser, {0xF0, 0x01, 0x02, 0x03, 0x04, 0x05, 0xF7});

    check(f.recorder.count() == 0);
    check(f.recorder.dropped.size() == 1);

    // The whole dump as it was seen: 0xF0, five data bytes, 0xF7.
    check(f.recorder.dropped[0] == 7);

    // And the stream carries on from the next status byte.
    f.recorder.feed(f.parser, {0x90, 0x3C, 0x64});

    check(f.recorder.count() == 1);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
};

auto tSysExAbandonedByStatus =
    test("MidiParser/abandonsAnUnterminatedSysExOnTheNextStatusByte") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0xF0, 0x43, 0x00, 0x01, 0x90, 0x3C, 0x64});

    check(f.recorder.dropped.size() == 1);
    check(f.recorder.dropped[0] == 4); // 0xF0 and three data bytes
    check(!f.parser.isInSysEx());

    // The status byte that ended it is a message in its own right.
    check(f.recorder.count() == 1);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
};

auto tSysExAbandonedByTimeout =
    test("MidiParser/abandonsAStalledSysExAfterTheTimeout") = []
{
    auto f = Fixture {};
    f.parser.setSysExTimeout(std::chrono::milliseconds {100});

    f.recorder.feed(f.parser, {0xF0, 0x43, 0x00}, 0);
    check(f.parser.isInSysEx());

    // Still inside the window: the dump is alive and keeps collecting.
    f.recorder.feed(f.parser, {0x01}, 50);
    check(f.parser.isInSysEx());
    check(f.recorder.dropped.empty());

    // 200ms since the last byte of the dump, so it is abandoned before the new
    // bytes are looked at - and those bytes still parse.
    f.recorder.feed(f.parser, {0x90, 0x3C, 0x64}, 250);

    check(f.recorder.dropped.size() == 1);
    check(f.recorder.dropped[0] == 4);
    check(!f.parser.isInSysEx());
    check(f.recorder.count() == 1);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
};

auto tSysExTimeoutOnEmptyFeed =
    test("MidiParser/anEmptyFeedIsEnoughToRunTheTimeout") = []
{
    // An owner whose port went quiet mid-dump can tick the parser rather than
    // wait for bytes that are never coming.
    auto f = Fixture {};
    f.parser.setSysExTimeout(std::chrono::milliseconds {100});

    f.recorder.feed(f.parser, {0xF0, 0x43, 0x00}, 0);
    f.recorder.feed(f.parser, {}, 500);

    check(!f.parser.isInSysEx());
    check(f.recorder.dropped.size() == 1);
    check(f.recorder.dropped[0] == 3);
};

auto tReset = test("MidiParser/resetForgetsEverythingInFlightSilently") = []
{
    auto f = Fixture {};

    f.recorder.feed(f.parser, {0x90, 0x3C, 0x64, 0xF0, 0x43, 0x00});
    f.parser.reset();

    check(!f.parser.isInSysEx());
    check(f.recorder.dropped.empty());

    // Running status is gone too, so the loose data bytes belong to nothing.
    f.recorder.feed(f.parser, {0x3E, 0x64});

    check(f.recorder.count() == 1);
    check(f.recorder.is(0, {0x90, 0x3C, 0x64}));
};

auto tNoBuffer = test("MidiParser/withNoBufferEverySysExIsDropped") = []
{
    auto parser = MidiParser {};
    auto recorder = Recorder {};

    recorder.feed(parser, {0xF0, 0x01, 0xF7, 0x90, 0x3C, 0x64});

    check(recorder.dropped.size() == 1);
    check(recorder.dropped[0] == 3);
    check(recorder.count() == 1);
    check(recorder.is(0, {0x90, 0x3C, 0x64}));
};
} // namespace
