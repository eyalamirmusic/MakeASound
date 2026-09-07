# Gaps

What `Apps/AudioProbe` finds about MakeASound, what has been fixed, and what is
still open. Findings about the library, not bugs in the probe.

macOS measured on 26.6 (arm64, Core Audio) with a Fireface UFX as the default
output, a Studio Display Microphone as the default input, and the interface
clocked at 44100. iOS measured on the iPhone 17 Pro simulator, iOS 26.5.

|  | first run | now |
| --- | --- | --- |
| macOS | 2 gaps, 4 pass, 3 waiting, 3 n/a | **0 gaps**, 10 pass, 2 waiting, 3 n/a |
| iOS | 7 gaps, 2 pass, 3 waiting, 0 n/a | **0 gaps**, 12 pass, 2 waiting, 1 n/a |

Thirteen probes became fifteen: `devices/default-input-flag` and
`callback/status-reported`, both of which the last round listed as worth adding
and both of which were red when they were written.

The two `waiting` rows are the same on both platforms. They have fixes in now —
what they are waiting for is a hardware event that proves them. See
[Still open](#still-open).

## Fixed

### The audio session belongs to the app

`Devices/AudioSession.h` is new: `SessionCategory`, `SessionOptions`,
`SessionConfig`, `SessionState`, and `hasAudioSession()` /`applySessionConfig()` /
`getSessionState()`. iOS implements it against `AVAudioSession`; everywhere else the
calls are no-ops and `hasAudioSession()` is false.

miniaudio no longer touches the session: the context is initialised with
`ma_ios_session_category_none` and `noAudioSessionActivate`, so constructing a
`DeviceManager` leaves it exactly where it was. The session is applied on every
open instead — including the ones recovery drives from its own thread, since an
interruption can hand it back deactivated.

The session **follows the stream** in every dimension the app has not spoken for:

- category — `Playback` with no input side, `PlayAndRecord` with one, so a
  playback-only app never asks for the microphone;
- `setPreferredSampleRate:` — the rate the `StreamConfig` asked for;
- `setPreferredIOBufferDuration:` — the block size it asked for.

`DeviceManager::setSessionConfig()` overrides any of it, including the mixing
options (`mixWithOthers`, `duckOthers`, `defaultToSpeaker`, `allowBluetooth`,
`allowAirPlay`) that decide whether the app shares the phone or stops the user's
music. Asking for a capture side under a category that cannot carry one is refused
with `INVALID_PARAMETER` rather than opening a stream that reads silence.

Fixes `session/owned-by-app` and `session/microphone-cost`. The probe app deleted
its own `AVAudioSession` wrapper and reads `MS::getSessionState()` instead.

### `getDefaultConfig()` no longer guesses a direction

Replaced by `getDefaultOutputConfig()`, `getDefaultInputConfig()` and
`getDefaultDuplexConfig()`. The old single entry point filled in whatever the
machine had, so the obvious first line an app writes claimed a capture side — which
costs a microphone permission on macOS and iOS both.

Fixes `config/playback-only`.

### iOS says which device the route is

`refreshDeviceCache` marks the first candidate as default when enumeration flagged
nothing, which is what `getDefaultOutputDevice()` fell back to anyway. iOS
enumerates the current route and flags nothing, so `isDefaultOutput` was false on
the only device there was.

Fixes `devices/default-flag`.

### The default input is the device the platform picked

miniaudio marks *two* capture devices `isDefault = 1` on Core Audio, and the first
cache entry carrying the flag won. Playback is enumerated first, so a merged duplex
entry beat the capture-only default: macOS said the default input was
`Studio Display Microphone`, MakeASound said the Fireface.

`getDefaultDeviceName(bool input)` in `Devices/DeviceQueries.h` asks the platform
directly — `kAudioHardwarePropertyDefaultInputDevice` on macOS, nothing to ask
elsewhere — and `resolveDefaults` now works down three sources in order of
authority: the platform's own answer, then the backend's flags, then the first
candidate with channels in that direction. Where the platform answers, the flag it
names is the only one set.

Found by the new `devices/default-input-flag` probe.
`DeviceManager/flagsOneDefaultPerDirection` is the regression test, and it needs no
particular hardware: it asserts that exactly one device is flagged in each
direction the machine has.

### iOS offers real rate choices

`getNativeFormat` now reports the rates `setPreferredSampleRate:` is worth asking
for (8000 … 48000) rather than only the one the session happens to be on, so a rate
picker has something to show and picking from it reaches the route.

Fixes `devices/rate-choices`.

### The negotiated block size is readable

`DeviceManager::getStreamBlockSize()`, beside `getStreamSampleRate()`. The backend
knew the negotiated value and dropped it on the way out; only `AudioCallbackInfo`
carried it. `getStreamLatency()` also returns `int` now, like everything else.

Fixes `stream/negotiated-block-size`.

### `getStreamLatency()` counts the route

It counted miniaudio's own periods and nothing else, which is the part of the delay
the backend can see rather than the part a caller wants to compensate for.
`getRouteLatency()` asks the platform for the rest — `kAudioDevicePropertyLatency`
plus the safety offset plus the stream's own latency on macOS,
`AVAudioSession`'s `outputLatency` / `inputLatency` on iOS — and the total is what
the manager and `AudioCallbackInfo::latency` report.

It is read once per open and cached: the property read is a HAL round-trip, and the
audio callback asks for the latency on every block. On the Fireface the route adds
65 frames on top of 768 of buffering.

### The MIDI facade returns errors instead of throwing

There was no `try` / `catch` anywhere under `Lib/MakeASound/RTMidi/`, so RtMidi
printed to stderr and threw straight through the public API — including out of
`MidiManager`'s constructor on iOS, where creating the MIDI client fails.

Every entry point now returns `Error`, virtual-port openers return
`std::optional<int>`, and `isAvailable()` / `getLastError()` say what happened. RtMidi
gets an error callback installed on every object, which suppresses both the print
and the throw; construction is still wrapped, since that is the one place the
callback cannot be installed in time. `sendMessage` with nothing open returns
`INVALID_USE` rather than silently succeeding.

Fixes `midi/virtual-port-errors`.

### The default config no longer re-clocks or resamples a shared device

`pickCompatibleSampleRate` consulted `preferredSampleRate` — a hardcoded
"48000, else 44100" — and never `currentSampleRate`, which the backend fills in
correctly. On this machine the Fireface runs at 44100, the default config asked for
48000, and miniaudio silently resampled: `getStreamSampleRate()` reported 48000
while the hardware stayed at 44100, and nothing in the API revealed the converter.

The rate a device is already clocked at now wins, since re-clocking a shared device
moves it under every other app using it.

Found by the new `stream/hardware-rate` probe, which compares
`getStreamSampleRate()` against `getCurrentSampleRate(device)`.

### Notifications reach a thread a UI can use

The notification callback runs wherever the OS raised the notification — on macOS
inside a Core Audio property listener, sometimes while recovery holds the device —
so no `DeviceManager` method may be called from it and every host wrote the same
marshalling to get off it.

`DeviceManager::drainNotifications()` hands back everything queued since the last
call, on the thread that asked: a UI timer, an idle callback, whatever the host
already has. The realtime callback is still there for a host that needs the news
sooner. Undrained notifications stop accumulating at 64.

Fixes `notify/main-thread-delivery`, which now passes without waiting for a device
to do anything: our own `start()` produces a `Started`, and the probe reads it off
the queue on the thread it draws from.

### `isRunning()` goes false when the OS stops the device

`streamRunning` was cleared only in `stopLocked()`. With auto-recover off nothing
else ever cleared it, so `isRunning()` answered `true` forever for a device that was
gone — the exact case an app turns auto-recover off to handle itself.

The `stopped` notification clears it now, and so does the watchdog when it decides
the device has starved. Neither path had anything to do with our own teardown, which
is still swallowed by the `stopping` flag.

### A block that missed its deadline says so

`AudioCallbackStatus` was hardcoded to `OK`, `getStatus()` was declared, defined and
never called, and `InputOverflow` was produced nowhere.

miniaudio's data callback carries no status of its own and the backends that know
about xruns handle them internally, so the clock is what is left: the gap between
one callback arriving and the next is the period plus whatever the last one overran
by, and a block that arrives a whole period late means the deadline was missed and
the OS filled the hole. Over 1029 blocks of 64 frames the measurement produced three
non-`OK` blocks for three deliberate stalls and no false positives.

`AudioCallbackInfo::errorCode` is gone rather than left at 0: nothing wrote it and
nothing could say what it would have meant.

`callback/status-reported` is the new probe, and it answers itself — the engine
holds one callback three block durations past its deadline once the stream has
settled, because a status nobody can provoke is a status nobody can trust. It costs
one glitch per run.

### The probe can pick a pair of outputs

`StreamParameters::firstChannel` / `nChannels` always confined audio to the selected
slice — verified on a 16-channel loopback device, at offset 0 and offset 4, with
every other channel silent — but nothing in the UI chose it. `UIDeviceManager` now
exposes `makeOutputChannelDropdown` / `makeInputChannelDropdown` (the underlying
`UI::` helpers already existed), and the probe has a channel picker and reports the
slice in its footer.

The device is still opened at its full native width, deliberately: that is what
makes `firstChannel` index real device channels rather than a renumbered subset.

## Still open

### The two live checks

`stream/running-after-os-stop` and `devices/route-stability` need a device to be
unplugged, re-clocked or interrupted. Both have fixes in; neither has been proven,
and the simulator produces neither event — backgrounding the app left audio running.

- `isRunning()` is cleared by the `stopped` notification now, so with auto-recover
  off it should report `false`. Unplugging something is what says whether the
  notification actually arrives on every path a device can die on.
- Device ids used to be enumeration order, so a cached `DeviceInfo::id` named a
  different device after a hotplug. They are a registry keyed on the device name
  now, handed back to the same name every time it is enumerated, and unique within
  one enumeration; `DeviceManager/handsTheSameIdToTheSameDevice` covers what can be
  covered without hardware, which is that repeated enumeration is stable and ids do
  not collide. A real hotplug is what would prove the rest.

On iOS these are routine rather than rare — an interruption is a phone call.

### `--strict` cannot exit on iOS

`eacp::Apps::quit()` reaches `CFRunLoopStop`, and `UIApplicationMain` restarts the
loop immediately (`Core/Threads/EventLoop-iOS.mm`). The probes run and log
correctly, but the process never exits and the count never reaches CI. This is
eacp's to fix, not MakeASound's. `--assert` still works.

### Unverified on hardware

The iOS block-size ladder is backed by `setPreferredIOBufferDuration:`, but only a
real route says what it grants. `latency/includes-route` now passes against a real
number rather than a near-zero simulator one, but the route it is checking is still
the simulator's; a phone with headphones in is what would exercise it.

## Probes

Fifteen, all of which pass or wait on hardware:

| probe | what it checks |
| --- | --- |
| `session/owned-by-app` | the app chooses the category and when the session activates |
| `session/microphone-cost` | a playback-only app needs no microphone permission |
| `config/playback-only` | a default config can be asked for one direction |
| `devices/default-flag` | `getDefaultOutputDevice()` comes back flagged |
| `devices/rate-choices` | the default output offers rates a picker could show |
| `devices/default-input-flag` | `getDefaultInputDevice()` names the device the platform does |
| `devices/route-stability` | a device id survives a route change |
| `stream/negotiated-block-size` | the block size the device runs is readable |
| `stream/running-after-os-stop` | `isRunning()` is false once the OS stopped the device |
| `stream/hardware-rate` | the reported rate is the rate the hardware runs |
| `notify/main-thread-delivery` | notifications arrive somewhere a UI can use them |
| `latency/includes-route` | `getStreamLatency()` counts the route's own latency |
| `midi/virtual-port-errors` | the MIDI facade reports failures like the audio one |
| `callback/dirty-on-shape-change` | the first block of a new shape reports dirty |
| `callback/status-reported` | a block that missed its deadline says so |
