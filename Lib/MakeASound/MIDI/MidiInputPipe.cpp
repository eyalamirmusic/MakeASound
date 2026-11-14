#include "MidiInputPipe.h"
#include "MIDI.h"

#include <chrono>
#include <optional>

namespace MakeASound
{
namespace
{
// SysEx is the one thing convertMidi does not carry, and the queue tier only
// takes a dump that fits an Event. Anything longer belongs in callback mode.
std::optional<MIDI::Event> toEvent(Span<const std::uint8_t> bytes) noexcept
{
    if (bytes.empty())
        return std::nullopt;

    if (bytes[0] == 0xF0)
    {
        if (bytes.size() > MIDI::SysEx::maxBytes)
            return std::nullopt;

        return MIDI::Event::sysEx(bytes.data(), bytes.size());
    }

    return MIDI::convertMidi(bytes.data(), bytes.size());
}
} // namespace

void MidiInputFlags::setIgnoredTypes(bool clock, bool activeSense) noexcept
{
    auto mask = static_cast<std::uint8_t>((clock ? ignoreClock : 0)
                                          | (activeSense ? ignoreActiveSense : 0));

    ignoredTypes.store(mask, std::memory_order_relaxed);
}

void MidiInputPipe::open(int portIdToUse,
                         const MidiInputCallback& callbackToUse,
                         int sysExBytes,
                         MidiTimePoint epochToUse,
                         MidiInputFlags& flagsToUse)
{
    portId = portIdToUse;
    callback = callbackToUse;
    flags = &flagsToUse;
    epoch = epochToUse;

    sysExStorage.resize(static_cast<std::size_t>(sysExBytes));
    parser.setSysExBuffer(Span<std::uint8_t>(sysExStorage));

    // One mode's storage, never both. The callback's message never outgrows the
    // dump the parser can assemble, so reserving here is what keeps assign() off
    // the heap on the MIDI thread; queue mode has no use for it, and callback
    // mode none for the queue.
    if (callback)
        scratch.bytes.reserve(sysExStorage.size());
    else
        queue.create();
}

void MidiInputPipe::feed(Span<const std::uint8_t> bytes,
                         MidiTimePoint timestamp) noexcept
{
    loadIgnoredTypes();
    parse(bytes, timestamp);
}

void MidiInputPipe::loadIgnoredTypes() noexcept
{
    auto mask = flags->ignoredTypes.load(std::memory_order_relaxed);

    parser.setIgnoredTypes((mask & MidiInputFlags::ignoreClock) != 0,
                           (mask & MidiInputFlags::ignoreActiveSense) != 0);
}

void MidiInputPipe::parse(Span<const std::uint8_t> bytes,
                          MidiTimePoint timestamp) noexcept
{
    auto onMessage = [this](const MidiMessageView& message) { deliver(message); };

    auto onDropped = [this](int)
    { flags->droppedSysEx.store(true, std::memory_order_relaxed); };

    parser.feed(bytes, timestamp, onMessage, onDropped);
}

void MidiInputPipe::deliver(const MidiMessageView& message) noexcept
{
    if (callback)
    {
        scratch.timestamp =
            std::chrono::duration<double>(message.timestamp - epoch).count();

        scratch.bytes.assign(message.bytes.begin(), message.bytes.end());
        callback(scratch);
        return;
    }

    auto typed = toEvent(message.bytes);

    if (!typed)
    {
        if (!message.bytes.empty() && message.bytes[0] == 0xF0)
            flags->droppedSysEx.store(true, std::memory_order_relaxed);

        return;
    }

    auto event = MidiInputEvent {portId, *typed, message.timestamp};

    if (!queue->push(event))
        flags->overflowed.store(true, std::memory_order_relaxed);
}

void MidiInputPipe::drainInto(Vector<MidiInputEvent>& out) noexcept
{
    if (callback)
        return;

    auto event = MidiInputEvent {};

    while (queue->pop(event))
        out.add(event);
}

} // namespace MakeASound
