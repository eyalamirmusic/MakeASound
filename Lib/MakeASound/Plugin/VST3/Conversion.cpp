#include "VST3Common.h"
#include "Conversion.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <algorithm>
#include <cmath>

namespace MakeASound::VST3
{

namespace
{
using Context = Vst::ProcessContext;

constexpr auto bendCentre = 8192;

float from7Bit(int value) noexcept
{
    return static_cast<float>(std::clamp(value, 0, 127)) / 127.f;
}

Steinberg::int8 to7Bit(float normalized) noexcept
{
    auto scaled = static_cast<int>(std::lround(normalized * 127.f));
    return static_cast<Steinberg::int8>(std::clamp(scaled, 0, 127));
}

void legacyCC(
    Vst::Event& out, int controller, int channel, int value, int value2 = 0) noexcept
{
    out.type = Vst::Event::kLegacyMIDICCOutEvent;
    out.midiCCOut.controlNumber = static_cast<Steinberg::uint8>(controller);
    out.midiCCOut.channel = static_cast<Steinberg::int8>(channel);
    out.midiCCOut.value = static_cast<Steinberg::int8>(value);
    out.midiCCOut.value2 = static_cast<Steinberg::int8>(value2);
}

bool fromLegacyCC(const Vst::LegacyMIDICCOutEvent& cc,
                  int offset,
                  MIDI::Event& out) noexcept
{
    auto channel = static_cast<int>(cc.channel);

    if (cc.controlNumber < 128)
    {
        out = MIDI::Event::controlChange(
            channel, cc.controlNumber, from7Bit(cc.value), offset);
        return true;
    }

    switch (cc.controlNumber)
    {
        case Vst::kAfterTouch:
            out =
                MIDI::Event::channelAftertouch(channel, from7Bit(cc.value), offset);
            return true;

        case Vst::kPitchBend:
        {
            auto raw = (cc.value & 0x7f) | ((cc.value2 & 0x7f) << 7);
            auto bend = static_cast<float>(raw - bendCentre) / bendCentre;
            out = MIDI::Event::pitchBend(channel, bend, offset);
            return true;
        }

        case Vst::kCtrlProgramChange:
            out = MIDI::Event::programChange(
                channel, std::clamp(static_cast<int>(cc.value), 0, 127), offset);
            return true;

        default:
            return false;
    }
}
} // namespace

Playhead toPlayhead(const Vst::ProcessContext& context) noexcept
{
    auto playhead = Playhead {};
    auto has = [&](Steinberg::uint32 flag) { return (context.state & flag) != 0; };

    playhead.isValid = true;
    playhead.isPlaying = has(Context::kPlaying);
    playhead.isRecording = has(Context::kRecording);
    playhead.isLooping = has(Context::kCycleActive);
    playhead.sampleTime = context.projectTimeSamples;

    if (has(Context::kProjectTimeMusicValid))
        playhead.ppqPosition = context.projectTimeMusic;

    if (has(Context::kBarPositionValid))
        playhead.barStartPpq = context.barPositionMusic;

    if (has(Context::kTempoValid))
        playhead.bpm = context.tempo;

    if (has(Context::kTimeSigValid))
        playhead.timeSignature = {context.timeSigNumerator,
                                  context.timeSigDenominator};

    if (has(Context::kCycleValid))
    {
        playhead.loopStartPpq = context.cycleStartMusic;
        playhead.loopEndPpq = context.cycleEndMusic;
    }

    return playhead;
}

bool fromVst3(const Vst::Event& in, MIDI::Event& out) noexcept
{
    auto offset = static_cast<int>(in.sampleOffset);

    switch (in.type)
    {
        case Vst::Event::kNoteOnEvent:
            out = MIDI::Event::noteOn(
                in.noteOn.channel, in.noteOn.pitch, in.noteOn.velocity, offset);
            return true;

        case Vst::Event::kNoteOffEvent:
            out = MIDI::Event::noteOff(
                in.noteOff.channel, in.noteOff.pitch, in.noteOff.velocity, offset);
            return true;

        case Vst::Event::kPolyPressureEvent:
            out = MIDI::Event::polyAftertouch(in.polyPressure.channel,
                                              in.polyPressure.pitch,
                                              in.polyPressure.pressure,
                                              offset);
            return true;

        case Vst::Event::kDataEvent:
        {
            auto size = static_cast<int>(in.data.size);

            if (in.data.type != Vst::DataEvent::kMidiSysEx
                || in.data.bytes == nullptr || size <= 0
                || size > MIDI::SysEx::maxBytes)
                return false;

            out = MIDI::Event::sysEx(in.data.bytes, size, offset);
            return true;
        }

        case Vst::Event::kLegacyMIDICCOutEvent:
            return fromLegacyCC(in.midiCCOut, offset, out);

        default:
            return false;
    }
}

bool toVst3(const MIDI::Event& in, int bus, Vst::Event& out) noexcept
{
    out = {};
    out.busIndex = bus;
    out.sampleOffset = in.sampleOffset;

    auto channel = static_cast<Steinberg::int16>(in.channel);

    return in.visit(MIDI::overloaded {
        [&](const MIDI::NoteOn& note)
        {
            out.type = Vst::Event::kNoteOnEvent;
            out.noteOn.channel = channel;
            out.noteOn.pitch = static_cast<Steinberg::int16>(note.pitch);
            out.noteOn.velocity = note.velocity;
            out.noteOn.noteId = -1;
            return true;
        },
        [&](const MIDI::NoteOff& note)
        {
            out.type = Vst::Event::kNoteOffEvent;
            out.noteOff.channel = channel;
            out.noteOff.pitch = static_cast<Steinberg::int16>(note.pitch);
            out.noteOff.velocity = note.velocity;
            out.noteOff.noteId = -1;
            return true;
        },
        [&](const MIDI::PolyAftertouch& pressure)
        {
            out.type = Vst::Event::kPolyPressureEvent;
            out.polyPressure.channel = channel;
            out.polyPressure.pitch = static_cast<Steinberg::int16>(pressure.pitch);
            out.polyPressure.pressure = pressure.pressure;
            out.polyPressure.noteId = -1;
            return true;
        },
        [&](const MIDI::ControlChange& cc)
        {
            legacyCC(
                out, std::clamp(cc.controller, 0, 127), channel, to7Bit(cc.value));
            return true;
        },
        [&](const MIDI::ChannelAftertouch& pressure)
        {
            legacyCC(out, Vst::kAfterTouch, channel, to7Bit(pressure.pressure));
            return true;
        },
        [&](const MIDI::PitchBend& bend)
        {
            auto raw = std::clamp(
                static_cast<int>(std::lround(bend.value * bendCentre)) + bendCentre,
                0,
                16383);
            legacyCC(out, Vst::kPitchBend, channel, raw & 0x7f, (raw >> 7) & 0x7f);
            return true;
        },
        [&](const MIDI::ProgramChange& program)
        {
            legacyCC(out,
                     Vst::kCtrlProgramChange,
                     channel,
                     std::clamp(program.program, 0, 127));
            return true;
        },
        [&](const MIDI::SysEx& sysEx)
        {
            out.type = Vst::Event::kDataEvent;
            out.data.type = Vst::DataEvent::kMidiSysEx;
            out.data.size = static_cast<Steinberg::uint32>(sysEx.size);
            out.data.bytes = sysEx.data.data();
            return true;
        }});
}

MIDI::Event
    shadowEvent(const ShadowTarget& target, int sampleOffset, float value) noexcept
{
    switch (target.controller)
    {
        case Vst::kAfterTouch:
            return MIDI::Event::channelAftertouch(
                target.channel, value, sampleOffset);

        case Vst::kPitchBend:
            return MIDI::Event::pitchBend(
                target.channel, value * 2.f - 1.f, sampleOffset);

        case Vst::kCtrlProgramChange:
            return MIDI::Event::programChange(
                target.channel,
                static_cast<int>(std::lround(value * 127.f)),
                sampleOffset);

        default:
            return MIDI::Event::controlChange(
                target.channel, target.controller, value, sampleOffset);
    }
}

Vst::SpeakerArrangement arrangementFor(int numChannels) noexcept
{
    if (numChannels <= 0)
        return Vst::SpeakerArr::kEmpty;

    if (numChannels == 1)
        return Vst::SpeakerArr::kMono;

    if (numChannels == 2)
        return Vst::SpeakerArr::kStereo;

    if (numChannels >= 64)
        return ~Vst::SpeakerArrangement {0};

    return (Vst::SpeakerArrangement {1} << numChannels) - 1;
}

} // namespace MakeASound::VST3
