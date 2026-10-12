// The ALSA backend's pure half: the conversions between the sequencer's events
// and the wire, which need alsa-lib but no sequencer. A container with no
// /dev/snd/seq runs every case here, and it is the only ALSA coverage such a
// machine gets - everything that needs a client of its own is in
// MidiManagerTests, where it skips.

#include <MakeASound/ALSA/ALSA-Backend.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <vector>

using namespace nano;

using MakeASound::Error;
using MakeASound::Span;

namespace ALSA = MakeASound::ALSA;

namespace
{
using Clock = std::chrono::steady_clock;

// alsa-lib's byte/event translator, which is all a conversion needs.
struct Coder
{
    Coder()
    {
        snd_midi_event_new(1024, &handle);

        if (handle != nullptr)
            snd_midi_event_no_status(handle, 1);
    }

    ~Coder()
    {
        if (handle != nullptr)
            snd_midi_event_free(handle);
    }

    Coder(const Coder&) = delete;
    Coder& operator=(const Coder&) = delete;

    snd_midi_event_t* handle {};
};

template <typename T, int (*Alloc)(T**), void (*Free)(T*)>
struct Info
{
    Info() { Alloc(&handle); }

    ~Info()
    {
        if (handle != nullptr)
            Free(handle);
    }

    Info(const Info&) = delete;
    Info& operator=(const Info&) = delete;

    T* handle {};
};

using ClientInfo = Info<snd_seq_client_info_t,
                        snd_seq_client_info_malloc,
                        snd_seq_client_info_free>;

using PortInfo =
    Info<snd_seq_port_info_t, snd_seq_port_info_malloc, snd_seq_port_info_free>;

// What `bytes` becomes once it has been through an event and back, fragments
// included: the whole of what an open output sends and an open input parses.
std::vector<std::uint8_t> roundTrip(const std::vector<std::uint8_t>& bytes)
{
    auto encoder = Coder {};
    auto decoder = Coder {};

    auto result = std::vector<std::uint8_t> {};
    auto storage = std::array<std::uint8_t, ALSA::maxShortMessageBytes> {};

    auto message = Span<const std::uint8_t>(bytes);
    auto sent = 0;

    while (sent < message.size())
    {
        auto event = snd_seq_event_t {};
        auto taken = ALSA::buildEvent(encoder.handle, message, sent, event);

        if (taken <= 0)
            break;

        auto decoded =
            ALSA::getEventBytes(decoder.handle, event, Span<std::uint8_t>(storage));

        result.insert(result.end(), decoded.begin(), decoded.end());
        sent += taken;
    }

    return result;
}

auto tErrors = test("Alsa/everyNegativeReturnBecomesAnError") = []
{
    check(ALSA::getError(0) == Error::NoError);
    check(ALSA::getError(1) == Error::NoError);

    // A kernel with no snd-seq and a port that went away answer the same way.
    check(ALSA::getError(-ENOENT) == Error::INVALID_DEVICE);
    check(ALSA::getError(-ENODEV) == Error::INVALID_DEVICE);

    check(ALSA::getError(-EINVAL) == Error::INVALID_PARAMETER);
    check(ALSA::getError(-EPERM) == Error::INVALID_USE);
    check(ALSA::getError(-ENOMEM) == Error::MEMORY_ERROR);
    check(ALSA::getError(-EIO) == Error::DRIVER_ERROR);
    check(ALSA::getError(-EDOM) == Error::UNKNOWN_ERROR);
};

auto tShort = test("Alsa/shortMessagesComeBackAsTheyWent") = []
{
    auto messages = std::vector<std::vector<std::uint8_t>> {
        {0x90, 0x3C, 0x64}, // note on
        {0x80, 0x3C, 0x40}, // note off
        {0xB0, 0x07, 0x7F}, // control change
        {0xC5, 0x20}, // program change
        {0xD2, 0x40}, // channel aftertouch
        {0xE0, 0x00, 0x40}, // pitch bend, centred
        {0xF2, 0x10, 0x20}, // song position
        {0xF8}, // timing clock
        {0xFE}, // active sensing
    };

    for (const auto& message: messages)
        check(roundTrip(message) == message);
};

auto tSysEx = test("Alsa/aDumpLeavesAsFragmentsThatAddUpToIt") = []
{
    // Longer than one event carries, so the send fragments it and the parser on
    // the other side is the one that puts it back together.
    constexpr auto size = ALSA::maxSysExChunkBytes * 2 + 37;

    auto dump = std::vector<std::uint8_t>(size, 0);

    dump.front() = 0xF0;
    dump.back() = 0xF7;

    for (auto i = 1; i < size - 1; ++i)
        dump[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i % 128);

    check(roundTrip(dump) == dump);
};

auto tEmpty = test("Alsa/nothingSendableYieldsNoEvent") = []
{
    auto coder = Coder {};
    auto event = snd_seq_event_t {};

    check(ALSA::buildEvent(coder.handle, {}, 0, event) == 0);

    // Half a message: nothing complete to send, and the coder is reset rather
    // than left holding it.
    auto partial = std::vector<std::uint8_t> {0x90};

    check(ALSA::buildEvent(coder.handle, Span<const std::uint8_t>(partial), 0, event)
          == 0);
};

auto tStamps = test("Alsa/aStampedEventSitsOnTheQueuesClock") = []
{
    auto queueEpoch = Clock::now();

    auto event = snd_seq_event_t {};
    event.flags = static_cast<unsigned char>(SND_SEQ_TIME_STAMP_REAL);
    event.time.time.tv_sec = 2;
    event.time.time.tv_nsec = 500000000;

    check(ALSA::toTimePoint(event, queueEpoch)
          == queueEpoch + std::chrono::milliseconds {2500});

    // An event the kernel never stamped is as old as the moment it is read.
    auto unstamped = snd_seq_event_t {};
    auto before = Clock::now();
    auto arrival = ALSA::toTimePoint(unstamped, queueEpoch);

    check(arrival >= before);
    check(arrival <= Clock::now());
};

auto tNames = test("Alsa/portNamesCarryTheClientAndPortNumbers") = []
{
    auto client = ClientInfo {};
    auto port = PortInfo {};

    if (client.handle == nullptr || port.handle == nullptr)
        return;

    snd_seq_client_info_set_name(client.handle, "Some Synth");
    snd_seq_port_info_set_name(port.handle, "MIDI 1");
    snd_seq_port_info_set_client(port.handle, 24);
    snd_seq_port_info_set_port(port.handle, 3);

    check(ALSA::getPortName(client.handle, port.handle) == "Some Synth:MIDI 1 24:3");

    check(ALSA::getPortIdentity(ALSA::Address {24, 3}) == "24:3");
};

auto tFilter = test("Alsa/ourOwnPlumbingIsNotAPortAHostCanOpen") = []
{
    auto filter = ALSA::PortFilter {128, 0, 1};

    check(filter.shouldSkip(ALSA::Address {128, 0}));
    check(filter.shouldSkip(ALSA::Address {128, 1}));

    // A virtual port of ours enumerates like anyone else's, which is what makes
    // it the loopback the tests open.
    check(!filter.shouldSkip(ALSA::Address {128, 2}));
    check(!filter.shouldSkip(ALSA::Address {24, 0}));
};
} // namespace
