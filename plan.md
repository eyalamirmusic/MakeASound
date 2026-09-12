# Native MIDI backend: migration plan

Replace RtMidi with platform backends the library owns, one platform at a time,
behind the existing `MidiManager` façade. Core MIDI (macOS + iOS) first, then ALSA,
then Windows, then delete RtMidi. Every phase lands on `main` with CI green on all
three desktop targets and the iOS simulator run.

## Why

Found while answering "do we allocate on SysEx?":

- RtMidi filters SysEx by default and we never turn it on, so the `MIDI::SysEx`
  type in the public API is never produced by the input side.
- With SysEx on, RtMidi's own accumulator (`std::vector`, never reserved) allocates
  on its input thread before our trampoline runs. Nothing on our side can reserve it.
- An unterminated SysEx makes RtMidi append every later message to the pending dump
  until a packet happens to end in 0xF7. No timeout. The port goes silent.
- Packet timestamps are reduced to a delta `double` we ignore. `MidiBlockSync`
  stamps events with `steady_clock::now()` at delivery instead, so sample offsets
  carry thread-scheduling jitter.
- No hotplug notification. The audio side has a two-path notification design that
  MIDI cannot feed.
- Errors are exceptions plus `std::cerr` prints from the input thread; the backend
  exists largely to contain that.
- `isAvailable()` is false on iOS because RtMidi's client creation fails there.
- No route to MIDI 2.0 / UMP, which Core MIDI has offered since macOS 11 / iOS 14.

## Target design

### Public surface (breaking changes are fine)

- `MidiManager` keeps its shape: enumerate, open input in queue or callback mode,
  virtual ports, one output, `drainMessages`, `sendMessage`. Comments that say
  "RtMidi's input thread" become "the platform's MIDI thread".
- `isAvailable()` becomes true on iOS.
- **Timestamps.** `MidiInputEvent::arrival` is the packet's hardware timestamp, not
  the delivery time. `MidiManager::now()` returns the same clock so `MidiBlockSync`
  compares like with like (Core MIDI stamps in `mach_absolute_time` units; ALSA in
  its queue clock; WinMM in ms since `midiInStart`). `MidiTimePoint` stays a
  `steady_clock::time_point`; each backend converts into it once per event.
- **SysEx policy.** Two tiers, both explicit:
  - Callback mode delivers a complete SysEx of any size up to a per-port assembly
    cap (`MidiManager::maxSysExBytes`, default 64 KiB, allocated when the port
    opens, never on the MIDI thread). Beyond the cap the dump is dropped and a
    `MidiNotification::SysExDropped` is raised.
  - Queue mode delivers a `MIDI::SysEx` event when the dump fits
    `SysEx::maxBytes`, otherwise drops it. Raise `maxBytes` from 16 to 32 so an
    identity reply with a 3-byte manufacturer id (17 bytes) fits.
  - An unterminated dump is abandoned when the next status byte other than a
    realtime one arrives, or after a timeout (default 1 s) measured on the MIDI
    thread's own clock. Realtime bytes (0xF8..0xFF) interleaved in a dump pass
    through and do not break it, per the spec.
- **Hotplug.** `MidiNotification` enum (`PortAdded`, `PortRemoved`,
  `SysExDropped`, `QueueOverflow`) with the same two paths as audio:
  `setNotificationCallback` (immediate, platform thread) and
  `drainNotifications()` (mutex-guarded queue, host thread).
- **Port ids.** A slot in a registry keyed by the platform's stable identity
  (Core MIDI `kMIDIPropertyUniqueID`, ALSA client:port, WinMM name+index), same
  rule as `idForDevice` on the audio side, so a hotplug does not renumber the
  other ports. Virtual ports keep negative ids.
- **`MidiPortInfo`** gains nothing yet. `manufacturer` / `isVirtual` can come later.
- **Timing clock and active sensing** stay filtered by default; add
  `setIgnoredTypes(bool clock, bool activeSense)` per manager for anyone who wants
  0xF8 at 24 ppqn.

### Internal structure

```
Lib/MakeASound/MIDI/
    MidiBackend.h        abstract interface the façade owns (replaces the
                         concrete RTMidi::MidiManager pimpl)
    MidiParser.{h,cpp}   platform-agnostic byte-stream parser: running status,
                         realtime interleave, bounded SysEx assembly. Pure,
                         allocation-free, unit-tested. Every backend feeds it.
    MidiPortRegistry.{h,cpp}  identity -> id slots, shared by all backends
Lib/MakeASound/CoreMIDI/
    CoreMIDIManager.{h,cpp}   owns MIDIClientRef, ports, notify proc
    CoreMIDI-Backend.{h,cpp}  pure conversions: OSStatus -> Error, packet list
                              walking, host-time -> MidiTimePoint, port naming
Lib/MakeASound/ALSA/         same split, Phase 3
Lib/MakeASound/WinMIDI/      same split, Phase 4
```

One `makeMidiBackend()` per platform TU, chosen in `Lib/CMakeLists.txt` the way
`DeviceQueries` already is. No `#ifdef` forests in shared code.

### Threading model on the input side

- The platform thread runs `MidiParser` over the raw bytes into fixed per-port
  buffers. No heap, no locks, no logging.
- Queue mode: each port owns an `SPSCQueue<MidiInputEvent, 2048>` (already in
  `Realtime/`). Producer is the platform thread, consumer is whoever calls
  `drainMessages`. This replaces the spinlock + `Vector` pair in `InputPort` and
  removes the "skipped while contended" caveat. A full queue drops the event and
  counts a `QueueOverflow` notification.
- Callback mode: the user callback receives a `MidiMessage` whose `bytes` is a
  per-port buffer reserved to `maxSysExBytes` at open. Unchanged contract: copy
  what you keep.
- Output: packet lists built on the stack, chunked at the platform's limit
  (64 KiB on Core MIDI), no async SysEx request objects. `sendMessage` stays
  synchronous and allocation-free for any size.

## Phases

### Phase 0: seam (no behaviour change)

1. Add `MIDI/MidiBackend.h`, an abstract class with the façade's operations.
2. Make `RTMidi::MidiManager` implement it. Façade holds
   `OwningPointer<MidiBackend>` created by `makeMidiBackend()`; today that returns
   RtMidi on every platform.
3. Add `MIDI/MidiPortRegistry` and route RtMidi's enumeration through it so ids
   become stable before any native backend exists.
4. Add `MidiManager::now()` and the notification API with RtMidi raising nothing.
5. Tests unchanged and green. Exit: `main` builds on all targets, `RTMidi/` is
   the only backend, nothing under `MIDI/` mentions RtMidi.

### Phase 1: `MidiParser` and its tests

1. `MidiParser` with a fixed SysEx buffer handed in by the owner (`Span<uint8_t>`),
   `feed(bytes, timestamp, sink)` emitting complete messages as views.
2. `Tests/MidiParserTests.cpp`: running status, three-byte and two-byte messages,
   realtime byte inside a SysEx, SysEx split across three feeds, oversize SysEx
   dropped with the byte count reported, unterminated SysEx abandoned on the next
   status byte and on timeout, garbage data bytes with no status skipped.
3. `Tests/AllocationTests.cpp`: the parser under the ban, including the SysEx
   assembly path. Exit: parser merged and used by nothing yet.

### Phase 2: Core MIDI backend (macOS + iOS)

1. `CoreMIDI/` per the layout above. Client created with
   `MIDIClientCreateWithBlock`; input ports with
   `MIDIInputPortCreateWithProtocol` (MIDI 1.0 protocol for now, which keeps the
   UMP door open) on macOS 11+ / iOS 14+, falling back to `MIDIInputPortCreate`
   below that only if the deployment target requires it.
2. Enumerate sources and destinations; registry keyed by unique id; names from
   `kMIDIPropertyDisplayName` with the RtMidi-style "device name" fallback so
   existing users see the same strings.
3. Virtual source / destination via `MIDISourceCreate` / `MIDIDestinationCreate`,
   which also works on iOS, so the "no virtual ports on iOS" note goes away.
4. Hotplug through the notify block, mapped to `PortAdded` / `PortRemoved`. The
   notify block is delivered on the run loop of the thread that created the
   client, so document that a process without a running `CFRunLoop` on that thread
   gets no hotplug; the Example CLI is such a process and should say so in a
   comment rather than pretend.
5. Timestamps: `mach_timebase_info` once, convert per packet into
   `steady_clock` nanoseconds. Verify in a test that `MidiManager::now()` and a
   loopback event's `arrival` agree to within a few ms.
6. Select Core MIDI on `APPLE` in `Lib/CMakeLists.txt`; RtMidi stays for the rest.
7. `Tests/RealtimeThreadAllocationTests.cpp`: extend the MIDI loopback tests with a
   SysEx of 1 KiB and one larger than `SysEx::maxBytes`, so both tiers are
   measured on the real platform thread. Add a queue-mode loopback that checks
   sample offsets from `MidiBlockSync` are monotonic and within the block.
8. Run the iOS simulator ctest job; `isAvailable()` must be true there.
9. Manual checklist before merge (no CI can do these): hotplug a USB device with
   the Synth app open, receive a DX7-size bulk dump (4104 bytes) in callback mode,
   pull the cable mid-dump and confirm notes still arrive afterwards, Bluetooth
   MIDI on iOS.

Exit: Apple builds link no RtMidi. `MidiDemo`, `Synth`, `AudioProbe` unchanged in
behaviour except that iOS now has MIDI.

### Phase 3: ALSA backend (Linux)

1. `ALSA/` using the sequencer API (`snd_seq_open`, one client, one input port,
   one output port), subscriptions instead of "opening" a port, virtual ports as
   plain sequencer ports. `snd_seq_event_input` already parses channel messages;
   SysEx arrives as `SND_SEQ_EVENT_SYSEX` fragments and goes through `MidiParser`
   so the policy is identical.
2. Hotplug via `SND_SEQ_EVENT_PORT_START` / `PORT_EXIT` on the announce port.
3. Timestamps from a sequencer queue clocked in realtime mode, converted to
   `steady_clock`.
4. One input thread per manager blocking on `poll`, which replaces RtMidi's
   thread. Same no-heap rule; the allocation suite runs on Linux already.
5. Select on `Linux`; CI GCC job goes green with `rtmidi` gone from that link.

### Phase 4: Windows backend

Decision needed before starting (see below). With WinMM:

1. `WinMIDI/` with `midiInOpen` + `MIM_DATA` for short messages and a ring of
   `MIDIHDR` buffers for `MIM_LONGDATA`, re-armed on the callback thread (WinMM
   forbids most calls there, but `midiInAddBuffer` is allowed). Feed everything
   through `MidiParser`.
2. No virtual ports and no hotplug on WinMM; `openVirtual*` return nullopt as
   today and the registry rescans on `getInputPorts()`. `PortAdded/Removed` are
   raised by diffing enumerations, which is what a UI polling the list gets anyway.
3. Timestamps: `MIM_DATA` carries ms since `midiInStart`; combine with a
   `QueryPerformanceCounter` taken at start.
4. Select on `WIN32`; MSVC and clang-cl jobs green.

### Phase 5: remove RtMidi

1. Delete `Lib/MakeASound/RTMidi/`, `CMake/FindRTMidi.cmake`,
   `find_package(RTMidi)`, and the `rtmidi` link.
2. Remove `Error::WARNING`, `THREAD_ERROR` and anything else that only existed to
   mirror `RtMidiError::Type`, if no native backend produces them.
3. Update `CLAUDE.md`: the `MIDI/` and backend sections, the allocation-test
   paragraph (which names `midiInputTrampoline`), the dependency list, and the
   `-DCPM_RTMidi_SOURCE` example.
4. `README` and façade comments.

## Decisions to make

Recommendations first.

1. **Windows API.** WinMM now; it works on every Windows we build for and CI has
   no MIDI hardware either way. Windows MIDI Services (Win 11 24H2+) brings
   virtual ports and hotplug but needs a newer SDK and a runtime that older
   machines lack. Keep the backend seam so it can be a second Windows TU later.
2. **`SysEx::maxBytes`.** Raise to 32. It still keeps `MIDI::Event` small enough to
   copy through the SPSC queue and covers every realtime-relevant message plus
   identity replies.
3. **Per-port SysEx cap.** 64 KiB default, settable before `openInput`. Anyone
   receiving sample dumps larger than that wants a file, not a callback.
4. **Core MIDI protocol.** Open ports as MIDI 1.0 now. UMP is a follow-up that
   only touches `CoreMIDI-Backend` and `MidiParser`.
5. **Callback thread contract.** Keep "callback runs on the platform thread, copy
   what you keep". Marshalling to another thread is the app's job, as today.

## Risks

- Core MIDI hotplug needs a run loop on the creating thread. Apps with a UI have
  one; the CLI examples do not. Documented, not solved.
- CI has no MIDI hardware. Virtual-port loopback covers the data path on macOS and
  Linux; Windows has no virtual ports so its input path is only covered by the
  parser tests and manual runs. Say so in `Tests/CMakeLists.txt` next to the
  existing "nothing was watching" note.
- Verify RtMidi's iOS failure was RtMidi's and not Core MIDI's before Phase 2 is
  planned as a win; a quick `MIDIClientCreate` in the simulator ctest job settles
  it and is the first thing Phase 2 does.
- `MidiMessage` is reflected by Miro (`MIRO_REFLECT(timestamp, bytes)`). Keeping
  `bytes` a `std::vector` preserves that; the per-port reservation keeps it off the
  heap. Do not swap it for a view without checking the Demo web UI's use.

## Order of work

Phase 0 and Phase 1 are independent of each other and both small; do them first
as separate PRs. Phase 2 is the bulk of the value and the one that needs hardware
on a desk. Phases 3 and 4 can run in parallel once Phase 2 has settled the
backend interface. Phase 5 is a deletion PR.
