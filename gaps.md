# Gaps

What `Apps/AudioProbe` finds about MakeASound, what has been fixed, and what is
still open. Findings about the library, not bugs in the probe.

macOS measured on 26.6 (arm64, Core Audio) with a Fireface UFX as the default
output, a Studio Display Microphone as the default input, and the interface
clocked at 44100. iOS measured on the iPhone 17 Pro simulator, iOS 26.5.

|  | before | now |
| --- | --- | --- |
| macOS | 2 gaps, 4 pass, 3 waiting, 3 n/a | **0 gaps**, 7 pass, 3 waiting, 3 n/a |
| iOS | 7 gaps, 2 pass, 3 waiting, 0 n/a | **0 gaps**, 10 pass, 3 waiting, 0 n/a |

The three `waiting` rows are the same on both platforms and are still open — they
need a hardware event no simulator produces. See [Still open](#still-open).

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

### The three live checks

`stream/running-after-os-stop`, `devices/route-stability` and
`notify/main-thread-delivery` need a device to be unplugged, re-clocked or
interrupted. Unchanged and still gaps:

- `streamRunning` is cleared only in `stopLocked()`, so with auto-recover off
  `isRunning()` returns `true` forever after an OS stop.
- Device ids are enumeration order, so a cached `DeviceInfo::id` names a different
  device after a hotplug.
- The notification callback runs on an OS audio thread, so every host writes the
  same marshalling.

On iOS these are routine rather than rare — an interruption is a phone call. The
simulator does not produce one: backgrounding the app left audio running.

### `getDefaultInputDevice()` still returns the wrong device on macOS

miniaudio marks *two* capture devices `isDefault = 1` on Core Audio, and the first
cache entry carrying the flag wins. Playback devices are enumerated first, so a
merged duplex entry beats a capture-only default: macOS says the default input is
`Studio Display Microphone`, MakeASound says the Fireface. `flagDefaultsIfUnmarked`
only acts when *nothing* is flagged, so it does not help here. No probe covers it.

### `AudioCallbackStatus` is always `OK`

`MiniAudioDeviceManager.cpp` hardcodes it and `getStatus()` in
`MiniAudio-Backend.cpp` is declared, defined and never called. `InputOverflow` is
produced nowhere. `AudioCallbackInfo::errorCode` is never written either.

### `--strict` cannot exit on iOS

`eacp::Apps::quit()` reaches `CFRunLoopStop`, and `UIApplicationMain` restarts the
loop immediately (`Core/Threads/EventLoop-iOS.mm`). The probes run and log
correctly, but the process never exits and the count never reaches CI. This is
eacp's to fix, not MakeASound's. `--assert` still works.

### Unverified on hardware

`latency/includes-route` passes against a simulator route whose `outputLatency` is
near zero; `getStreamLatency()` counts only miniaudio's internal periods, so expect
it to flip on a device. The iOS block-size ladder is now backed by
`setPreferredIOBufferDuration:`, but only a real route says what it grants.

## Probes

Twelve became thirteen: `stream/hardware-rate` is new. Two more worth adding:

| probe | what it would check | red today |
| --- | --- | --- |
| `devices/default-input-flag` | `getDefaultInputDevice()` against the platform's own default input | macOS |
| `callback/status-reported` | that `AudioCallbackStatus` can ever be anything but `OK` | both |
