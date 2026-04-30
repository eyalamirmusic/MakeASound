#include "MIDI.h"

#include "../Common/Algorithms.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace MakeASound::MIDI
{

// --- Buffer ------------------------------------------------------------

void Buffer::addFrom(const Buffer& other) noexcept
{
    reserveAtLeast(other.size());

    for (auto& e: other)
        add(e);
}

void Buffer::sortByOffset() noexcept
{
    Algorithms::stableInsertionSort(*this);
}

// --- Event factories ---------------------------------------------------

Event Event::noteOn(int channel,
                    int pitch,
                    float velocity,
                    int sampleOffset) noexcept
{
    auto event = Event {};
    event.sampleOffset = sampleOffset;
    event.channel = channel;
    event.payload = NoteOn {.pitch = pitch, .velocity = velocity};
    return event;
}

Event Event::noteOff(int channel,
                     int pitch,
                     float velocity,
                     int sampleOffset) noexcept
{
    auto event = Event {};
    event.sampleOffset = sampleOffset;
    event.channel = channel;
    event.payload = NoteOff {.pitch = pitch, .velocity = velocity};
    return event;
}

Event Event::controlChange(int channel,
                           int controller,
                           float value,
                           int sampleOffset) noexcept
{
    auto event = Event {};
    event.sampleOffset = sampleOffset;
    event.channel = channel;
    event.payload = ControlChange {.controller = controller, .value = value};
    return event;
}

Event Event::pitchBend(int channel, float value, int sampleOffset) noexcept
{
    auto event = Event {};
    event.sampleOffset = sampleOffset;
    event.channel = channel;
    event.payload = PitchBend {.value = value};
    return event;
}

Event Event::channelAftertouch(int channel,
                               float pressure,
                               int sampleOffset) noexcept
{
    auto event = Event {};
    event.sampleOffset = sampleOffset;
    event.channel = channel;
    event.payload = ChannelAftertouch {.pressure = pressure};
    return event;
}

Event Event::polyAftertouch(int channel,
                            int pitch,
                            float pressure,
                            int sampleOffset) noexcept
{
    auto event = Event {};
    event.sampleOffset = sampleOffset;
    event.channel = channel;
    event.payload = PolyAftertouch {.pitch = pitch, .pressure = pressure};
    return event;
}

Event Event::programChange(int channel, int program, int sampleOffset) noexcept
{
    auto event = Event {};
    event.sampleOffset = sampleOffset;
    event.channel = channel;
    event.payload = ProgramChange {.program = program};
    return event;
}

Event Event::sysEx(const uint8_t* bytes, int size, int sampleOffset) noexcept
{
    auto event = Event {};
    event.sampleOffset = sampleOffset;
    event.channel = -1;

    auto sysEx = SysEx {};

    // Oversize / malformed input yields an empty SysEx so audio-thread
    // callers can still push the (empty) event without branching.
    assert(bytes != nullptr && size >= 0 && size <= SysEx::maxBytes
           && "SysEx payload exceeds MIDI::SysEx::maxBytes");

    if (bytes != nullptr && size > 0 && size <= SysEx::maxBytes)
    {
        std::memcpy(sysEx.data.data(), bytes, static_cast<size_t>(size));
        sysEx.size = size;
    }

    event.payload = sysEx;
    return event;
}

// --- Queries -----------------------------------------------------------

bool Event::isNoteOn() const noexcept
{
    return std::holds_alternative<NoteOn>(payload);
}
bool Event::isNoteOff() const noexcept
{
    return std::holds_alternative<NoteOff>(payload);
}
bool Event::isControlChange() const noexcept
{
    return std::holds_alternative<ControlChange>(payload);
}
bool Event::isPitchBend() const noexcept
{
    return std::holds_alternative<PitchBend>(payload);
}
bool Event::isChannelAftertouch() const noexcept
{
    return std::holds_alternative<ChannelAftertouch>(payload);
}
bool Event::isPolyAftertouch() const noexcept
{
    return std::holds_alternative<PolyAftertouch>(payload);
}
bool Event::isProgramChange() const noexcept
{
    return std::holds_alternative<ProgramChange>(payload);
}
bool Event::isSysEx() const noexcept
{
    return std::holds_alternative<SysEx>(payload);
}

// --- Typed accessors ---------------------------------------------------

const NoteOn* Event::asNoteOn() const noexcept
{
    return std::get_if<NoteOn>(&payload);
}
const NoteOff* Event::asNoteOff() const noexcept
{
    return std::get_if<NoteOff>(&payload);
}
const ControlChange* Event::asControlChange() const noexcept
{
    return std::get_if<ControlChange>(&payload);
}
const PitchBend* Event::asPitchBend() const noexcept
{
    return std::get_if<PitchBend>(&payload);
}
const ChannelAftertouch* Event::asChannelAftertouch() const noexcept
{
    return std::get_if<ChannelAftertouch>(&payload);
}
const PolyAftertouch* Event::asPolyAftertouch() const noexcept
{
    return std::get_if<PolyAftertouch>(&payload);
}
const ProgramChange* Event::asProgramChange() const noexcept
{
    return std::get_if<ProgramChange>(&payload);
}
const SysEx* Event::asSysEx() const noexcept
{
    return std::get_if<SysEx>(&payload);
}

// --- Conversion --------------------------------------------------------

std::optional<Event> convertMidi(const MidiMessage& msg, int sampleOffset) noexcept
{
    if (msg.bytes.empty())
        return std::nullopt;

    auto status = static_cast<int>(msg.bytes[0] & 0xF0);
    auto channel = static_cast<int>(msg.bytes[0] & 0x0F);

    auto byteAt = [&](size_t i) -> int
    { return i < msg.bytes.size() ? static_cast<int>(msg.bytes[i]) : 0; };

    if (status == 0x90 && msg.bytes.size() >= 3)
    {
        auto vel = byteAt(2);
        if (vel == 0)
            return Event::noteOff(channel, byteAt(1), 0.f, sampleOffset);
        return Event::noteOn(
            channel, byteAt(1), static_cast<float>(vel) / 127.f, sampleOffset);
    }
    if (status == 0x80 && msg.bytes.size() >= 3)
        return Event::noteOff(
            channel, byteAt(1), static_cast<float>(byteAt(2)) / 127.f, sampleOffset);
    if (status == 0xB0 && msg.bytes.size() >= 3)
        return Event::controlChange(
            channel, byteAt(1), static_cast<float>(byteAt(2)) / 127.f, sampleOffset);
    if (status == 0xE0 && msg.bytes.size() >= 3)
    {
        auto raw = (byteAt(2) << 7) | byteAt(1);
        auto bend = (static_cast<float>(raw) - 8192.f) / 8192.f;
        return Event::pitchBend(channel, bend, sampleOffset);
    }
    if (status == 0xD0 && msg.bytes.size() >= 2)
        return Event::channelAftertouch(
            channel, static_cast<float>(byteAt(1)) / 127.f, sampleOffset);
    if (status == 0xA0 && msg.bytes.size() >= 3)
        return Event::polyAftertouch(
            channel, byteAt(1), static_cast<float>(byteAt(2)) / 127.f, sampleOffset);
    if (status == 0xC0 && msg.bytes.size() >= 2)
        return Event::programChange(channel, byteAt(1), sampleOffset);

    return std::nullopt;
}

// --- Rendering ---------------------------------------------------------

std::string toString(const Event& event)
{
    char buf[192];

    auto renderChannel = [&]() -> std::string
    {
        if (event.channel < 0)
            return {};
        char c[16];
        std::snprintf(c, sizeof c, " ch=%d", event.channel);
        return c;
    };

    return event.visit(overloaded {
        [&](const NoteOn& n)
        {
            std::snprintf(buf,
                          sizeof buf,
                          "NoteOn%s pitch=%d vel=%.3f @%d",
                          renderChannel().c_str(),
                          n.pitch,
                          static_cast<double>(n.velocity),
                          event.sampleOffset);
            return std::string {buf};
        },
        [&](const NoteOff& n)
        {
            std::snprintf(buf,
                          sizeof buf,
                          "NoteOff%s pitch=%d vel=%.3f @%d",
                          renderChannel().c_str(),
                          n.pitch,
                          static_cast<double>(n.velocity),
                          event.sampleOffset);
            return std::string {buf};
        },
        [&](const ControlChange& cc)
        {
            std::snprintf(buf,
                          sizeof buf,
                          "CC%s #%d value=%.3f @%d",
                          renderChannel().c_str(),
                          cc.controller,
                          static_cast<double>(cc.value),
                          event.sampleOffset);
            return std::string {buf};
        },
        [&](const PitchBend& pb)
        {
            std::snprintf(buf,
                          sizeof buf,
                          "PitchBend%s value=%.3f @%d",
                          renderChannel().c_str(),
                          static_cast<double>(pb.value),
                          event.sampleOffset);
            return std::string {buf};
        },
        [&](const ChannelAftertouch& at)
        {
            std::snprintf(buf,
                          sizeof buf,
                          "ChannelAftertouch%s pressure=%.3f @%d",
                          renderChannel().c_str(),
                          static_cast<double>(at.pressure),
                          event.sampleOffset);
            return std::string {buf};
        },
        [&](const PolyAftertouch& pa)
        {
            std::snprintf(buf,
                          sizeof buf,
                          "PolyAftertouch%s pitch=%d pressure=%.3f @%d",
                          renderChannel().c_str(),
                          pa.pitch,
                          static_cast<double>(pa.pressure),
                          event.sampleOffset);
            return std::string {buf};
        },
        [&](const ProgramChange& pc)
        {
            std::snprintf(buf,
                          sizeof buf,
                          "ProgramChange%s program=%d @%d",
                          renderChannel().c_str(),
                          pc.program,
                          event.sampleOffset);
            return std::string {buf};
        },
        [&](const SysEx& sx)
        {
            auto out = std::string {"SysEx ["};
            for (auto i = 0; i < sx.size; ++i)
            {
                char byte[6];
                std::snprintf(byte,
                              sizeof byte,
                              "%s%02X",
                              i == 0 ? "" : " ",
                              sx.data[static_cast<std::size_t>(i)]);
                out += byte;
            }
            char tail[32];
            std::snprintf(tail, sizeof tail, "] @%d", event.sampleOffset);
            out += tail;
            return out;
        },
    });
}

} // namespace MakeASound::MIDI
