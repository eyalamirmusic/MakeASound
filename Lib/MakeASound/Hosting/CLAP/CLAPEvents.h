#pragma once

#include <clap/events.h>

#include <vector>

namespace MakeASound::Hosting
{

// One event of any core type the host sends or keeps, as the lists below store
// it. An event larger than this (another space's, a future type) is refused.
union CLAPEvent
{
    clap_event_header header;
    clap_event_note note;
    clap_event_note_expression expression;
    clap_event_param_value param;
    clap_event_param_mod mod;
    clap_event_param_gesture gesture;
    clap_event_transport transport;
    clap_event_midi midi;
    clap_event_midi_sysex sysEx;
    clap_event_midi2 midi2;
};

// The events a hosted CLAP plugin reads in one block or flush: a fixed number,
// reserved on the message thread, then filled, sorted and cleared on the audio
// thread without allocating. A sysex event's bytes are the caller's and must
// outlive the block. Internal to the CLAP format.
class CLAPInputEvents
{
public:
    CLAPInputEvents();

    CLAPInputEvents(const CLAPInputEvents&) = delete;
    CLAPInputEvents& operator=(const CLAPInputEvents&) = delete;

    // Room for `capacity` events, emptied. Message thread, never while a block
    // runs: it allocates.
    void reserve(int capacity);

    int capacity() const noexcept { return static_cast<int>(storage.size()); }
    int size() const noexcept { return count; }
    bool empty() const noexcept { return count == 0; }

    // A copy of the event, last in the list. False, and nothing kept, when the
    // list is full or the event is larger than a CLAPEvent.
    bool add(const clap_event_header& event) noexcept;

    // The event at `index` in the list's order (sorted, once sortByTime() ran),
    // or null past the end.
    const clap_event_header* at(int index) const noexcept;

    // The same event, for the host to amend in place before the block runs.
    CLAPEvent* mutableAt(int index) noexcept;

    // Orders the events by time, as CLAP requires of a host, stably: events at
    // one time keep the order they were added in. An insertion sort over an
    // index, so it neither allocates nor moves an event.
    void sortByTime() noexcept;

    void clear() noexcept { count = 0; }

    const clap_input_events* get() const noexcept { return &list; }

private:
    std::vector<CLAPEvent> storage;
    std::vector<int> order;
    int count = 0;
    clap_input_events list {};
};

// What a hosted CLAP plugin pushes in one block or flush, up to a fixed number:
// past it, try_push refuses, so the host never allocates. A sysex event's bytes
// stay the plugin's, valid only until process() or flush() returns, so they are
// read before then. Internal to the CLAP format.
class CLAPOutputEvents
{
public:
    CLAPOutputEvents();

    CLAPOutputEvents(const CLAPOutputEvents&) = delete;
    CLAPOutputEvents& operator=(const CLAPOutputEvents&) = delete;

    // Room for `capacity` events, emptied. Message thread, as for the input.
    void reserve(int capacity);

    int size() const noexcept { return count; }
    const clap_event_header* at(int index) const noexcept;

    void clear() noexcept { count = 0; }

    const clap_output_events* get() const noexcept { return &list; }

private:
    bool push(const clap_event_header* event) noexcept;

    std::vector<CLAPEvent> storage;
    int count = 0;
    clap_output_events list {};
};

} // namespace MakeASound::Hosting
