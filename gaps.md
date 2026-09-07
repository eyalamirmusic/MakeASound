# Gaps

What `Apps/AudioProbe` shows about MakeASound on macOS, and what the same machine
shows next to it. These are findings about the library, not bugs in the probe.

Measured on macOS 26.6 (arm64, Core Audio) with a Fireface UFX as the default
output, a Studio Display Microphone as the default input, and the interface
clocked at 44100.

```
$ ./build/Apps/AudioProbe/AudioProbe --strict
2 gaps, 4 pass, 3 waiting, 3 n/a
```

## Scored gaps

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

## Unscored — no probe covers these yet

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

## Probes worth adding

| probe | what it would check |
| --- | --- |
| `devices/default-input-flag` | `getDefaultInputDevice()` against the platform's own default input |
| `stream/hardware-rate` | `getStreamSampleRate()` against `getCurrentSampleRate(device)` |
| `callback/status-reported` | that `AudioCallbackStatus` can ever be anything but `OK` |

All three turn red on macOS today.
