#include "Conversion.h"

#include <algorithm>
#include <cmath>

namespace MakeASound::AUv3
{

int sampleOffset(AUEventSampleTime time,
                 const AudioTimeStamp* timestamp,
                 AUAudioFrameCount frames) noexcept
{
    if (frames == 0)
        return 0;

    auto last = static_cast<AUEventSampleTime>(frames) - 1;

    // AUEventSampleTimeImmediate plus a small offset means that far into the block.
    if (auto immediate = time - AUEventSampleTimeImmediate;
        immediate >= 0 && immediate < 0x7fffffff)
        return static_cast<int>(std::min(immediate, last));

    if (timestamp == nullptr)
        return 0;

    auto start = static_cast<AUEventSampleTime>(timestamp->mSampleTime);
    return static_cast<int>(std::clamp<AUEventSampleTime>(time - start, 0, last));
}

std::optional<MIDI::Event> midiEvent(const AUMIDIEvent& event, int offset) noexcept
{
    if (event.length == 0)
        return std::nullopt;

    // A scheduled dump may arrive typed as plain MIDI; its first byte says.
    if (event.eventType == AURenderEventMIDISysEx || event.data[0] == 0xf0)
    {
        if (event.length > MIDI::SysEx::maxBytes)
            return std::nullopt;

        return MIDI::Event::sysEx(event.data, event.length, offset);
    }

    return MIDI::convertMidi(event.data, std::min<int>(event.length, 3), offset);
}

Playhead readPlayhead(__unsafe_unretained AUHostMusicalContextBlock musicalContext,
                      __unsafe_unretained AUHostTransportStateBlock transportState,
                      const AudioTimeStamp* timestamp) noexcept
{
    auto playhead = Playhead {};

    auto tempo = 0.0;
    auto numerator = 0.0;
    auto denominator = NSInteger {};
    auto beat = 0.0;
    auto toNextBeat = NSInteger {};
    auto downbeat = 0.0;

    if (musicalContext != nil
        && musicalContext(
            &tempo, &numerator, &denominator, &beat, &toNextBeat, &downbeat))
    {
        playhead.isValid = true;
        playhead.ppqPosition = beat;
        playhead.barStartPpq = downbeat;

        if (tempo > 0.0)
            playhead.bpm = tempo;

        if (numerator > 0.0 && denominator > 0)
            playhead.timeSignature = {static_cast<int>(std::lround(numerator)),
                                      static_cast<int>(denominator)};
    }

    auto flags = AUHostTransportStateFlags {};
    auto position = 0.0;
    auto cycleStart = 0.0;
    auto cycleEnd = 0.0;

    if (transportState != nil
        && transportState(&flags, &position, &cycleStart, &cycleEnd))
    {
        playhead.isValid = true;
        playhead.isPlaying = (flags & AUHostTransportStateMoving) != 0;
        playhead.isRecording = (flags & AUHostTransportStateRecording) != 0;
        playhead.isLooping = (flags & AUHostTransportStateCycling) != 0;
        playhead.sampleTime = static_cast<int64_t>(position);
        playhead.loopStartPpq = cycleStart;
        playhead.loopEndPpq = cycleEnd;
    }
    else if (timestamp != nullptr
             && (timestamp->mFlags & kAudioTimeStampSampleTimeValid) != 0)
    {
        playhead.sampleTime = static_cast<int64_t>(timestamp->mSampleTime);
    }

    return playhead;
}

} // namespace MakeASound::AUv3
