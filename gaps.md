# Gaps

What `Apps/AudioProbe` shows about MakeASound, and what the same machines show
next to it. These are findings about the library, not bugs in the probe.

macOS measured on 26.6 (arm64, Core Audio) with a Fireface UFX as the default
output, a Studio Display Microphone as the default input, and the interface
clocked at 44100. iOS measured on the iPhone 17 Pro simulator, iOS 26.5.

```
macOS   2 gaps, 4 pass, 3 waiting, 3 n/a
iOS     7 gaps, 2 pass, 3 waiting, 0 n/a
```

The three `waiting` rows are the same three on both platforms, and they are gaps
on both — see [Live checks](#live-checks--gaps-that-need-a-hardware-event-to-fire).

## macOS — scored gaps

### `config/playback-only`

`getDefaultConfig()` fills the input side whenever the machine has one, so the
obvious first thing a caller writes — `start(getDefaultConfig(), cb)` — opens a
capture stream it never asked for. On macOS that trips TCC: a bundled app without
`NSMicrophoneUsageDescription` is terminated. `Apps/AudioProbe/Info-macOS.plist.in`
carries the key only because of this. A playback-only caller has to know to call
`config.input.reset()`.

There is no `getDefaultOutputConfig()`, and nothing in the API says the key is
needed.

### `stream/negotiated-block-size`

The negotiated block size is not missing, it is dropped.
`MiniAudio/MiniAudioDeviceManager.cpp:557` writes it into the **backend's** copy of
the config; `Devices/DeviceManager` keeps a separate copy that still holds whatever
the caller asked for. `Devices/DeviceManager.h:58-59` offers `getStreamSampleRate()`
with no block-size counterpart, and there is no `getConfig()` at all — which is why
`Apps/AudioProbe/AudioEngine` shadows the whole `StreamConfig` itself.

`getStreamLatency()` also returns `long` where the rest of the API is `int`.

## Live checks — gaps that need a hardware event to fire

Same three rows on macOS and iOS.

These read `waiting` in a short run because nothing has unplugged a device yet. The
backend says all three fire on macOS.

### `stream/running-after-os-stop`

`streamRunning` is cleared only in `stopLocked()`
(`MiniAudio/MiniAudioDeviceManager.cpp:479`). With auto-recover off,
`onNotification(stopped)` does not request recovery and `isStarved()` early-outs on
`!autoRecover`, so nothing clears it: `isRunning()` returns `true` forever after the
OS stops the device. `Devices/DeviceInfo.h` already admits it — "nothing else
reveals one: it still reports itself started".

### `devices/route-stability`

Device ids are enumeration order (`entry.id = nextId++`,
`MiniAudio/MiniAudioDeviceManager.cpp:298-303`). Unplug a device and every id above
it shifts, so a cached `DeviceInfo::id` opens something else. The backend has
`repointConfigToCache()` to survive this internally by name; nothing repoints the
caller's copies.

### `notify/main-thread-delivery`

Documented in `Devices/DeviceManager.h:52-54`: the notification callback runs on an
OS audio thread, from a Core Audio property listener, and calling any
`DeviceManager` method from it can deadlock. The starvation path proves it
independently — `notifyHost(DeviceNotification::Stopped)` is called from the
recovery worker (`MiniAudio/MiniAudioDeviceManager.cpp:762`). Every host has to
write the same marshalling `AudioEngine` writes.

## macOS — unscored, no probe covers these yet

### `getDefaultInputDevice()` returns the wrong device

macOS reports `Studio Display Microphone` as the default input
(`kAudioHardwarePropertyDefaultInputDevice`). MakeASound returns the Fireface.

miniaudio marks two capture devices `isDefault = 1` on Core Audio, and
`getDefaultInputDevice()` (`MiniAudio/MiniAudioDeviceManager.cpp:372`) takes the
first cache entry carrying the flag. Playback devices are enumerated first, so the
merged duplex entry always wins over a capture-only default.

`devices/default-flag` only checks the output side, which passes.

### The default config silently resamples

The Fireface runs at 44100. `getDefaultConfig()` asks for 48000, and the stream
opens with the hardware still at 44100: miniaudio resamples, and nothing in the API
says so.

- `pickCompatibleSampleRate` (`Devices/DeviceInfo.cpp:128`) consults
  `preferredSampleRate`, and `pickPreferredSampleRate` is a hardcoded
  "48000, else 44100, else the first rate". Neither looks at `currentSampleRate`,
  which `MiniAudio/MiniAudioDeviceManager.cpp:264` fills in correctly.
- `getStreamSampleRate()` and `AudioCallbackInfo::sampleRate` both report
  `device.sampleRate` (`MiniAudio/MiniAudioDeviceManager.cpp:653`) — the requested
  rate, not `internalSampleRate`.
- `getStreamLatency()` multiplies internal periods without the resampler's delay,
  and reports hardware-rate frames as if they were stream frames.

On a device that another app can move, the default path re-clocks or resamples
without a word.

### `AudioCallbackStatus` is always `OK`

`MiniAudio/MiniAudioDeviceManager.cpp:659` hardcodes it. `getStatus()`
(`MiniAudio/MiniAudio-Backend.cpp:42`) is declared, defined and never called, and
`InputOverflow` is produced nowhere. The probe's underflow and overflow counters are
structurally zero. `AudioCallbackInfo::errorCode` is never written either.

### The MIDI facade throws where the audio facade returns

There is no `try` / `catch` anywhere under `Lib/MakeASound/RTMidi/`. On macOS:

```
openInput(9999)                    -> prints to stderr, throws RtMidiError
sendMessage with no open output    -> returns normally, does nothing
```

Two failure modes, neither of them `Error`. `midi/virtual-port-errors` scores `pass`
only because virtual ports happen to work on Core MIDI; the same call throws on iOS
and Windows.

## iOS — scored gaps

Verbatim from the simulator run (`xcrun simctl launch --console-pty booted
com.eyalamir.makeasound.audioprobe --strict`):

```
GAP session/owned-by-app
  DeviceManager's constructor moved the session from SoloAmbient to PlayAndRecord
  and its output channels from 2 to 2; MakeASound exposes no way to choose either
GAP session/microphone-cost
  the session is PlayAndRecord and this bundle carries NSMicrophoneUsageDescription
  to survive it
GAP config/playback-only
  the default config claims 'MicrophoneBuiltIn' (2 ch)
GAP devices/default-flag
  'Speaker' came back with isDefaultOutput = false; the manager fell back to the
  first device that had outputs
GAP devices/rate-choices
  'Speaker' offers one rate (48000): the session's current rate is all the backend
  reports
GAP stream/negotiated-block-size
  asked for 256, the device runs 256; only AudioCallbackInfo says so
GAP midi/virtual-port-errors
  openVirtualOutput threw: MidiOutCore::initialize: error creating OS-X virtual
  MIDI source.
```

`config/playback-only` and `stream/negotiated-block-size` are the two macOS gaps
above, unchanged. The other five are iOS-only.

### The audio session belongs to miniaudio, not the app

Constructing a `DeviceManager` moves `AVAudioSession` from `SoloAmbient` to
`PlayAndRecord` and activates it, before the caller has said anything about what
it wants. Nothing in the API chooses the category, the options, or the moment of
activation, and there is no way to hand back a session the app already configured.

Everything else on iOS follows from that:

- `PlayAndRecord` is why a playback-only app still needs
  `NSMicrophoneUsageDescription` in its bundle, and why it appears in the iOS
  recording indicator.
- Mixing behaviour is not reachable. `MixWithOthers`, `DuckOthers`,
  `DefaultToSpeaker`, `AllowBluetoothA2DP` and `AllowAirPlay` are the difference
  between an app that shares the phone and one that stops the user's music.
- `setPreferredSampleRate:` and `setPreferredIOBufferDuration:` are the only way
  to ask iOS for a rate or a block size, and neither is exposed.

### iOS never flags a default device

`devices/default-flag` fails because iOS enumeration never sets `isDefaultOutput`,
so `getDefaultOutputDevice()` (`MiniAudio/MiniAudioDeviceManager.cpp:388`) falls
through to "the first device that has outputs". It happens to be `Speaker`. The
macOS version of this bug is the input side returning the wrong device; on iOS
neither side is flagged at all.

### One sample rate, and no way to ask for another

`getNativeFormat` (`Devices/DeviceQueriesIOS.mm`) reports a single rate — whatever
`AVAudioSession` is at right now — and `buildDeviceInfo` early-returns with
`sampleRates = {that}`. So `deviceSupportsSampleRate` is false for every other
rate, a rate picker has one entry, and `UIDeviceManager::makeSampleRateDropdown`
has nothing to offer.

The comment on the early return is about avoiding RemoteIO's aborting RPC, which is
the right call. The gap is that nothing replaces it: asking the session for a
different rate is the iOS way to change it, and that call is not exposed.

### Block sizes are a guess

`getSupportedBlockSizes` on iOS (`Devices/DeviceQueriesIOS.mm`) returns a
hardcoded 64..2048 ladder — "iOS takes a preferred IO duration rather than a frame
count, and grants whatever the route allows, so there is no list to read back".
The dropdown therefore offers sizes the route may not grant, and there is no
`setPreferredIOBufferDuration:` behind it to ask with. The simulator granted the
requested 256; hardware routes often do not.

### The MIDI facade throws, and here it actually fires

`openVirtualOutput` throws on iOS. RtMidi also prints
`MidiOutCore::initialize: error creating OS-X virtual MIDI source.` to stderr
before throwing, which the probe has no way to suppress. Same defect as on macOS
(no `try` / `catch` under `Lib/MakeASound/RTMidi/`); iOS is where it stops being
theoretical.

## iOS — unscored, no probe covers these yet

### `--strict` cannot exit, so there is no CI mode on iOS

The probes run and log correctly, but `eacp::Apps::quit()` reaches
`EventLoop::quit()`, which is `CFRunLoopStop(CFRunLoopGetCurrent())`
(`Core/Threads/EventLoop-iOS.mm:98-105`) — and `UIApplicationMain` immediately
starts the loop again. The process stays alive; the exit code is never delivered.

Verified: after the gap dump was logged, the process was still running, and a
`simctl launch --console-pty` never returned. The README's iOS invocation
therefore hangs and reports nothing to CI. `--assert` still works, since `assert`
aborts the process outright.

### `latency/includes-route` passes only in the simulator

It is one of the two iOS passes: `getStreamLatency()` reported 768 frames (16 ms
at 48 kHz) against a simulator route whose `outputLatency` and `IOBufferDuration`
are near zero. On hardware the route's own latency is much larger, and
`getStreamLatency()` counts only miniaudio's internal periods
(`MiniAudio/MiniAudioDeviceManager.cpp:592-603`). Expect this row to flip on a
device.

### Interruptions are untested

Backgrounding the app on the simulator did not produce an interruption — audio
kept running with no `audio` background mode in the bundle. `InterruptionBegan`,
`InterruptionEnded` and the reroute path are exactly what
`stream/running-after-os-stop` and `notify/main-thread-delivery` are there to
catch, and they are routine on a phone rather than rare. They need hardware.

## Probes worth adding

| probe | what it would check | red today |
| --- | --- | --- |
| `devices/default-input-flag` | `getDefaultInputDevice()` against the platform's own default input | macOS |
| `stream/hardware-rate` | `getStreamSampleRate()` against `getCurrentSampleRate(device)` | macOS |
| `callback/status-reported` | that `AudioCallbackStatus` can ever be anything but `OK` | both |
| `session/mixing-options` | that an app can ask to mix with other audio | iOS |
| `probe/exits` | that `--strict` returns an exit code | iOS |
