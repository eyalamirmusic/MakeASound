#include "Conversion.h"

#include <algorithm>

namespace MakeASound::AU
{

Playhead readPlayhead(const au::AUBase& unit,
                      const AudioTimeStamp& timestamp) noexcept
{
    auto playhead = Playhead {};

    auto beat = Float64 {};
    auto tempo = Float64 {};

    if (unit.CallHostBeatAndTempo(&beat, &tempo) == noErr)
    {
        playhead.isValid = true;
        playhead.ppqPosition = beat;

        if (tempo > 0.0)
            playhead.bpm = tempo;
    }

    auto deltaToNextBeat = UInt32 {};
    auto numerator = Float32 {};
    auto denominator = UInt32 {};
    auto downBeat = Float64 {};

    if (unit.CallHostMusicalTimeLocation(
            &deltaToNextBeat, &numerator, &denominator, &downBeat)
        == noErr)
    {
        playhead.isValid = true;
        playhead.barStartPpq = downBeat;

        if (numerator > 0.f && denominator > 0)
            playhead.timeSignature = {static_cast<int>(numerator),
                                      static_cast<int>(denominator)};
    }

    auto isPlaying = Boolean {};
    auto changed = Boolean {};
    auto sampleInTimeline = Float64 {};
    auto isCycling = Boolean {};
    auto cycleStart = Float64 {};
    auto cycleEnd = Float64 {};

    if (unit.CallHostTransportState(&isPlaying,
                                    &changed,
                                    &sampleInTimeline,
                                    &isCycling,
                                    &cycleStart,
                                    &cycleEnd)
        == noErr)
    {
        playhead.isValid = true;
        playhead.isPlaying = isPlaying != 0;
        playhead.isLooping = isCycling != 0;
        playhead.sampleTime = static_cast<int64_t>(sampleInTimeline);
        playhead.loopStartPpq = cycleStart;
        playhead.loopEndPpq = cycleEnd;
    }
    else if ((timestamp.mFlags & kAudioTimeStampSampleTimeValid) != 0)
    {
        playhead.sampleTime = static_cast<int64_t>(timestamp.mSampleTime);
    }

    return playhead;
}

float from7Bit(UInt8 value) noexcept
{
    return static_cast<float>(std::min<int>(value, 127)) / 127.f;
}

float fromPitchWheel(UInt8 lsb, UInt8 msb) noexcept
{
    auto raw = (lsb & 0x7f) | ((msb & 0x7f) << 7);
    return std::clamp(static_cast<float>(raw - 8192) / 8192.f, -1.f, 1.f);
}

std::optional<MIDI::Event> sysExEvent(const UInt8* data, UInt32 length) noexcept
{
    if (data == nullptr || length == 0 || length > MIDI::SysEx::maxBytes)
        return std::nullopt;

    return MIDI::Event::sysEx(data, static_cast<int>(length));
}

MIDIPacket* addPacket(MIDIPacketList& list,
                      ByteCount capacity,
                      MIDIPacket* current,
                      const MIDI::Event& event) noexcept
{
    auto bytes = MIDI::toBytes(event);

    if (bytes.size <= 0)
        return current;

    auto offset = static_cast<MIDITimeStamp>(std::max(0, event.sampleOffset));

    return MIDIPacketListAdd(&list,
                             capacity,
                             current,
                             offset,
                             static_cast<ByteCount>(bytes.size),
                             bytes.data.data());
}

} // namespace MakeASound::AU
