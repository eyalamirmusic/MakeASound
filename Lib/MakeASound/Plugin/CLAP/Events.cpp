#include "Events.h"
#include "Adapter.h"

#include <clap/fixedpoint.h>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace MakeASound::CLAP
{

namespace
{
constexpr auto beatFactor = static_cast<double>(CLAP_BEATTIME_FACTOR);
constexpr auto secondFactor = static_cast<double>(CLAP_SECTIME_FACTOR);

int channelOf(int16_t channel) noexcept
{
    return channel >= 0 && channel < 16 ? channel : 0;
}

bool fromNote(const clap_event_note& note,
              uint16_t type,
              int time,
              MIDI::Event& out) noexcept
{
    if (note.key < 0 || note.key > 127)
        return false;

    auto channel = channelOf(note.channel);
    auto velocity = static_cast<float>(std::clamp(note.velocity, 0.0, 1.0));

    if (type == CLAP_EVENT_NOTE_ON)
        out = MIDI::Event::noteOn(channel, note.key, velocity, time);
    else if (type == CLAP_EVENT_NOTE_OFF)
        out = MIDI::Event::noteOff(channel, note.key, velocity, time);
    else
        out = MIDI::Event::noteOff(channel, note.key, 0.f, time);

    return true;
}

clap_event_header headerOf(uint32_t size, uint16_t type, uint32_t time) noexcept
{
    return {size, time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
}

int clampedTime(uint32_t time, int numSamples) noexcept
{
    auto last = std::max(0, numSamples - 1);
    return static_cast<int>(std::min(time, static_cast<uint32_t>(last)));
}
} // namespace

bool fromClap(const clap_event_header& header,
              int time,
              MIDI::Event& out,
              int& port) noexcept
{
    if (header.space_id != CLAP_CORE_EVENT_SPACE_ID)
        return false;

    switch (header.type)
    {
        case CLAP_EVENT_NOTE_ON:
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
        {
            const auto& note = reinterpret_cast<const clap_event_note&>(header);
            port = note.port_index;
            return fromNote(note, header.type, time, out);
        }

        case CLAP_EVENT_MIDI:
        {
            const auto& midi = reinterpret_cast<const clap_event_midi&>(header);
            auto converted = MIDI::convertMidi(midi.data, 3, time);

            if (!converted)
                return false;

            port = midi.port_index;
            out = *converted;
            return true;
        }

        case CLAP_EVENT_MIDI_SYSEX:
        {
            const auto& sysEx =
                reinterpret_cast<const clap_event_midi_sysex&>(header);

            if (sysEx.buffer == nullptr || sysEx.size == 0
                || sysEx.size > static_cast<uint32_t>(MIDI::SysEx::maxBytes))
                return false;

            port = sysEx.port_index;
            out =
                MIDI::Event::sysEx(sysEx.buffer, static_cast<int>(sysEx.size), time);
            return true;
        }

        default:
            return false;
    }
}

Playhead toPlayhead(const clap_event_transport& transport,
                    double sampleRate) noexcept
{
    auto playhead = Playhead {};
    auto has = [&](uint32_t flag) { return (transport.flags & flag) != 0; };

    playhead.isValid = true;
    playhead.isPlaying = has(CLAP_TRANSPORT_IS_PLAYING);
    playhead.isRecording = has(CLAP_TRANSPORT_IS_RECORDING);
    playhead.isLooping = has(CLAP_TRANSPORT_IS_LOOP_ACTIVE);

    if (has(CLAP_TRANSPORT_HAS_TEMPO))
        playhead.bpm = transport.tempo;

    if (has(CLAP_TRANSPORT_HAS_BEATS_TIMELINE))
    {
        playhead.ppqPosition =
            static_cast<double>(transport.song_pos_beats) / beatFactor;
        playhead.barStartPpq = static_cast<double>(transport.bar_start) / beatFactor;
        playhead.loopStartPpq =
            static_cast<double>(transport.loop_start_beats) / beatFactor;
        playhead.loopEndPpq =
            static_cast<double>(transport.loop_end_beats) / beatFactor;
    }

    if (has(CLAP_TRANSPORT_HAS_SECONDS_TIMELINE))
    {
        auto seconds =
            static_cast<double>(transport.song_pos_seconds) / secondFactor;
        playhead.sampleTime =
            static_cast<int64_t>(std::llround(seconds * sampleRate));
    }

    if (has(CLAP_TRANSPORT_HAS_TIME_SIGNATURE))
        playhead.timeSignature = {static_cast<int>(transport.tsig_num),
                                  static_cast<int>(transport.tsig_denom)};

    return playhead;
}

bool pushMidi(const clap_output_events& out,
              const MIDI::Event& event,
              int port,
              uint32_t time) noexcept
{
    auto portIndex = static_cast<uint16_t>(std::max(0, port));

    if (const auto* sysEx = event.asSysEx())
    {
        if (sysEx->size <= 0)
            return false;

        auto message = clap_event_midi_sysex {};
        message.header =
            headerOf(sizeof(clap_event_midi_sysex), CLAP_EVENT_MIDI_SYSEX, time);
        message.port_index = portIndex;
        message.buffer = sysEx->data.data();
        message.size = static_cast<uint32_t>(sysEx->size);
        return out.try_push(&out, &message.header);
    }

    auto bytes = MIDI::toBytes(event);

    if (bytes.size <= 0 || bytes.size > 3)
        return false;

    auto message = clap_event_midi {};
    message.header = headerOf(sizeof(clap_event_midi), CLAP_EVENT_MIDI, time);
    message.port_index = portIndex;

    for (auto i = 0; i < bytes.size; ++i)
        message.data[i] = bytes.data[static_cast<size_t>(i)];

    return out.try_push(&out, &message.header);
}

bool pushParamValue(const clap_output_events& out,
                    clap_id id,
                    double value,
                    uint32_t time) noexcept
{
    auto event = clap_event_param_value {};
    event.header =
        headerOf(sizeof(clap_event_param_value), CLAP_EVENT_PARAM_VALUE, time);
    event.param_id = id;
    event.cookie = nullptr;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    return out.try_push(&out, &event.header);
}

bool pushGesture(const clap_output_events& out,
                 clap_id id,
                 bool begin,
                 uint32_t time) noexcept
{
    auto event = clap_event_param_gesture {};
    event.header =
        headerOf(sizeof(clap_event_param_gesture),
                 static_cast<uint16_t>(begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                             : CLAP_EVENT_PARAM_GESTURE_END),
                 time);
    event.param_id = id;
    return out.try_push(&out, &event.header);
}

void Adapter::collectParameters(const clap_input_events& in, int numSamples) noexcept
{
    auto count = in.size(&in);

    for (auto i = uint32_t {0}; i < count; ++i)
    {
        const auto* header = in.get(&in, i);

        if (header != nullptr && header->space_id == CLAP_CORE_EVENT_SPACE_ID
            && header->type == CLAP_EVENT_PARAM_VALUE)
            pushParameterValue(
                *reinterpret_cast<const clap_event_param_value*>(header),
                numSamples);
    }
}

void Adapter::collectMidi(const clap_input_events& in, int numSamples) noexcept
{
    auto numBuses = pluginWrapper.busLayout().midiInputs.size();
    auto count = in.size(&in);

    for (auto i = uint32_t {0}; i < count; ++i)
    {
        const auto* header = in.get(&in, i);

        if (header == nullptr || header->space_id != CLAP_CORE_EVENT_SPACE_ID)
            continue;

        auto event = MIDI::Event {};
        auto port = -1;

        if (!fromClap(*header, clampedTime(header->time, numSamples), event, port))
            continue;

        // A wildcard or unknown port is the first, as VST3's adapter does.
        pluginWrapper.pushMidiIn(port >= 0 && port < numBuses ? port : 0, event);
    }
}

// A step at the event's time; the host's own values are the baseline, so they
// are not sent back.
void Adapter::pushParameterValue(const clap_event_param_value& event,
                                 int numSamples) noexcept
{
    auto index = indexOfClapId(event.param_id);

    if (index < 0)
        return;

    const auto& param = pluginWrapper.plugin().parameters()[index];
    auto normalized = toNormalized(param, mappings[index], event.value);

    pluginWrapper.pushParameterChange(
        index, clampedTime(event.header.time, numSamples), normalized);
    takeAsBaseline(index);
}

void Adapter::flush(const clap_input_events* in,
                    const clap_output_events* out) noexcept
{
    assert(active ? isAudioThread() : isMainThread());

    if (in != nullptr)
    {
        auto count = in->size(in);

        for (auto i = uint32_t {0}; i < count; ++i)
        {
            const auto* header = in->get(in, i);

            if (header != nullptr && header->space_id == CLAP_CORE_EVENT_SPACE_ID
                && header->type == CLAP_EVENT_PARAM_VALUE)
                pushParameterValue(
                    *reinterpret_cast<const clap_event_param_value*>(header), 0);
        }
    }

    // Everything completes now and the list is left empty.
    pluginWrapper.applyParameterChanges(0);

    for (auto index: pluginWrapper.appliedParameters())
        takeAsBaseline(index);

    if (!pluginWrapper.appliedParameters().empty())
        requestLatencyCheck();

    if (out != nullptr)
    {
        emitEdits(*out);
        emitPluginMoves(*out);
    }
}

// Every edit the editor made since the last drain, at the start of this block.
// Whatever the host's list refuses stays pending for the next drain.
void Adapter::emitEdits(const clap_output_events& out) noexcept
{
    // Cleared before the slots are read: an edit made meanwhile sets it again.
    if (!editsPending.exchange(false))
        return;

    auto complete = true;

    for (auto index: listed)
        if (!emitEditsOf(out, index))
            complete = false;

    if (!complete)
        editsPending.store(true);
}

// One parameter's gestures in order, each begin before the latest value and the
// value before its end; false when the host refused an event.
bool Adapter::emitEditsOf(const clap_output_events& out, int index) noexcept
{
    auto& slot = editSlots[static_cast<size_t>(index)];
    auto id = pluginWrapper.plugin().parameters().entry(index).clapId;

    // Ends first: an end is counted only after its begin, so ends <= begins.
    auto ends = slot.ends.load();
    auto begins = slot.begins.load();

    while (true)
    {
        auto open = slot.sentBegins != slot.sentEnds;

        if (!open)
        {
            if (slot.sentBegins == begins)
                break;

            if (!pushGesture(out, id, true))
                return false;

            ++slot.sentBegins;
            continue;
        }

        if (!emitEditValueOf(out, index))
            return false;

        if (slot.sentEnds == ends)
            break;

        if (!pushGesture(out, id, false))
            return false;

        ++slot.sentEnds;
    }

    // A value performed outside any gesture.
    return emitEditValueOf(out, index);
}

bool Adapter::emitEditValueOf(const clap_output_events& out, int index) noexcept
{
    auto& slot = editSlots[static_cast<size_t>(index)];

    if (!slot.dirty.exchange(false))
        return true;

    const auto& entry = pluginWrapper.plugin().parameters().entry(index);
    auto normalized = slot.value.load(std::memory_order_relaxed);

    if (pushParamValue(out,
                       entry.clapId,
                       toPublished(*entry.param, mappings[index], normalized)))
        return true;

    slot.dirty.store(true);
    return false;
}

// One value at the block's start per listed parameter that moved since the host
// last heard of it: a preset the plugin loaded itself, a randomize, a MIDI learn.
// A held parameter is the editor's gesture, which its own events report, and
// one that reports no plugin moves is left for the host to read when told.
void Adapter::emitPluginMoves(const clap_output_events& out) noexcept
{
    const auto& list = pluginWrapper.plugin().parameters();

    for (auto index: listed)
    {
        auto value = pluginWrapper.getNormalizedParameter(index);
        auto& last = lastPluginValue[static_cast<size_t>(index)];

        if (value == last.load(std::memory_order_relaxed))
            continue;

        last.store(value, std::memory_order_relaxed);

        const auto& entry = list.entry(index);

        if (pluginWrapper.isParameterHeld(index) || !entry.reportsPluginMoves)
            continue;

        pushParamValue(
            out, entry.clapId, toPublished(*entry.param, mappings[index], value));
    }
}

// The MIDI out buses merged by time, each sorted by process() (stably, so a
// note-off keeps its place ahead of a note-on at the same offset), after the
// parameter events at the block's start.
void Adapter::emitMidi(const clap_output_events& out, int numSamples) noexcept
{
    const auto& buses = pluginWrapper.midiOut();
    auto numBuses = std::min(buses.size(), midiCursors.size());

    for (auto bus = 0; bus < numBuses; ++bus)
        midiCursors[bus] = 0;

    while (true)
    {
        auto next = -1;

        for (auto bus = 0; bus < numBuses; ++bus)
        {
            if (midiCursors[bus] >= buses[bus].size())
                continue;

            if (next < 0
                || buses[bus][midiCursors[bus]].sampleOffset
                       < buses[next][midiCursors[next]].sampleOffset)
                next = bus;
        }

        if (next < 0)
            return;

        const auto& event = buses[next][midiCursors[next]++];
        auto time = static_cast<uint32_t>(
            std::clamp(event.sampleOffset, 0, std::max(0, numSamples - 1)));
        pushMidi(out, event, next, time);
    }
}

} // namespace MakeASound::CLAP
