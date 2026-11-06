#include "Conversion.h"

#include <algorithm>
#include <cstring>

namespace MakeASound::AAX
{

Playhead readPlayhead(const AAX_ITransport& transport) noexcept
{
    auto playhead = Playhead {};

    if (auto tempo = 0.0;
        transport.GetCurrentTempo(&tempo) == AAX_SUCCESS && tempo > 0.0)
    {
        playhead.isValid = true;
        playhead.bpm = tempo;
    }

    if (auto numerator = int32_t {}, denominator = int32_t {};
        transport.GetCurrentMeter(&numerator, &denominator) == AAX_SUCCESS
        && numerator > 0 && denominator > 0)
    {
        playhead.isValid = true;
        playhead.timeSignature = {numerator, denominator};
    }

    if (auto playing = false; transport.IsTransportPlaying(&playing) == AAX_SUCCESS)
    {
        playhead.isValid = true;
        playhead.isPlaying = playing;
    }

    if (auto sample = int64_t {};
        transport.GetCurrentNativeSampleLocation(&sample) == AAX_SUCCESS)
    {
        playhead.isValid = true;
        playhead.sampleTime = sample;
    }

    if (auto ticks = int64_t {};
        transport.GetCurrentTickPosition(&ticks) == AAX_SUCCESS)
    {
        playhead.isValid = true;
        playhead.ppqPosition = static_cast<double>(ticks) / ticksPerQuarter;
    }

    auto looping = false;
    auto loopStart = int64_t {};
    auto loopEnd = int64_t {};

    if (transport.GetCurrentLoopPosition(&looping, &loopStart, &loopEnd)
        == AAX_SUCCESS)
    {
        playhead.isValid = true;
        playhead.isLooping = looping;
        playhead.loopStartPpq = static_cast<double>(loopStart) / ticksPerQuarter;
        playhead.loopEndPpq = static_cast<double>(loopEnd) / ticksPerQuarter;
    }

    return playhead;
}

Playhead advance(Playhead playhead, int samples, double sampleRate) noexcept
{
    playhead.sampleTime += samples;

    if (playhead.isPlaying && sampleRate > 0.0)
        playhead.ppqPosition += samples / sampleRate * playhead.bpm / 60.0;

    return playhead;
}

void readMidi(const AAX_CMidiStream& stream,
              MidiParser& parser,
              MidiTimePoint now,
              int numSamples,
              MIDI::Buffer& into) noexcept
{
    if (stream.mBuffer == nullptr)
        return;

    auto offset = 0;

    auto add = [&](const MidiMessageView& message)
    {
        if (into.size() >= into.capacity())
            return;

        auto size = static_cast<int>(message.bytes.size());

        if (size > 0 && message.bytes[0] == 0xf0)
        {
            if (size <= MIDI::SysEx::maxBytes)
                into.add(MIDI::Event::sysEx(message.bytes.data(), size, offset));

            return;
        }

        if (auto event = MIDI::convertMidi(message.bytes.data(), size, offset))
            into.add(*event);
    };

    for (auto i = uint32_t {0}; i < stream.mBufferSize; ++i)
    {
        const auto& packet = stream.mBuffer[i];
        auto length = std::min<uint32_t>(packet.mLength, sizeof(packet.mData));
        offset = std::clamp(
            static_cast<int>(packet.mTimestamp), 0, std::max(0, numSamples - 1));

        parser.feed(Span<const std::uint8_t>(packet.mData, static_cast<int>(length)),
                    now,
                    add);
    }
}

std::optional<AAX_CMidiPacket> packetFor(const MIDI::Event& event,
                                         int sampleOffset) noexcept
{
    if (event.isSysEx())
        return std::nullopt;

    auto bytes = MIDI::toBytes(event);

    if (bytes.size <= 0 || bytes.size > 3 || (bytes.data[0] & 0x80) == 0
        || bytes.data[0] >= 0xf0)
        return std::nullopt;

    auto packet = AAX_CMidiPacket {};
    packet.mTimestamp = static_cast<uint32_t>(std::max(0, sampleOffset));
    packet.mLength = static_cast<uint32_t>(bytes.size);
    std::memcpy(packet.mData, bytes.data.data(), static_cast<size_t>(bytes.size));
    packet.mIsImmediate = false;
    return packet;
}

} // namespace MakeASound::AAX
