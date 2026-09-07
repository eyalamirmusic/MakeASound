# MakeASound

A C++20 static library that gives platform-agnostic access to audio devices and MIDI ports.

Two façades make up the whole public surface:

- **`MakeASound::DeviceManager`** — audio device enumeration and streaming, backed by [miniaudio](https://github.com/mackron/miniaudio).
- **`MakeASound::MidiManager`** — MIDI input/output ports, backed by [RtMidi](https://github.com/thestk/rtmidi).

Each façade hides its backend behind a pimpl, so no backend type ever leaks into a header you include.

```cpp
#include <MakeASound/MakeASound.h>

namespace MS = MakeASound;

auto manager = MS::DeviceManager {};
manager.start(manager.getDefaultConfig(),
              [](MS::AudioCallbackInfo& info)
              {
                  for (auto channel: info.getOutput())
                      channel.fill(0.f);
              });
```

## What you get

- **Device enumeration** across every audio API the machine offers (Core Audio, WASAPI, DirectSound, WinMM, ALSA, PulseAudio, JACK, AAudio, OpenSL, ...), with the API selectable at runtime.
- **Planar audio buffers.** The backend de-interleaves on the way in and re-interleaves on the way out, so a callback only ever sees channel-major data.
- **Errors, not exceptions.** A machine with no device, or with one that is busy, is an ordinary desktop state: `start`/`setConfig` return an `Error` and `getErrorMessage` turns it into something a user can read.
- **Automatic recovery.** A device stopped by the OS — sample-rate change, unplug, reclaim — is re-opened on a worker thread; a `dirty` flag on the callback tells you when to re-derive anything you cached.
- **Typed MIDI.** `MIDI::Event` is a variant over note on/off, CC, pitch bend, aftertouch, program change and short SysEx, with allocation-free conversion to and from raw bytes.
- **Block-aligned MIDI.** `MidiBlockSync` resolves arrival times into sample offsets inside the current audio block.
- **Real-time safe pieces.** `SPSCQueue`, `MidiManager::drainMessages`, `MIDI::Buffer::sortByOffset` and the `Algorithms` helpers neither allocate nor lock.

## Requirements

- CMake 3.31+ and a C++20 compiler.
- macOS 11+, iOS 15+, Windows (x64 and ARM64), or Linux. CI builds macOS universal (arm64 + x86_64), iOS device + simulator, Windows with MSVC and clang-cl on both architectures, and Linux with GCC and Clang.
- Linux additionally needs the ALSA development headers: `sudo apt-get install libasound2-dev`.

Dependencies are fetched by [CPM.cmake](CMake/CPM.cmake) on the first configure — nothing to install by hand.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build

./build/Apps/Example/Example      # streams white noise for 2 seconds
./build/Apps/MidiDemo/MidiDemo    # opens virtual MIDI ports and sends notes
open ./build/Apps/AudioProbe/AudioProbe.app   # the GPU/UI probe, see below
```

`Example` takes an optional driver name (`./Example "Core Audio"`, `./Example jack`) and prints what is available.

`Apps/Demo` and `Apps/Synth` build macOS `.app` bundles with React/Vite UIs hosted through [eacp](https://github.com/eyalamirmusic/eacp)'s webview; their web assets are bundled at build time.

### Options

| Option | Default | Effect |
| --- | --- | --- |
| `MAKEASOUND_BUILD_APPS` | `ON` | Build the example/demo apps (top-level builds only). |
| `MAKEASOUND_BUILD_TESTS` | `ON` | Build the unit tests (top-level builds only). |
| `MAKEASOUND_UNITY_BUILD` | `OFF` | Jumbo build of the library. |

To develop against a local checkout of a dependency instead of the fetched copy, pass e.g. `-DCPM_RTMidi_SOURCE=/path/to/rtmidi` at configure time.

### Tests

```bash
ctest --test-dir build --output-on-failure
```

## Using it in your project

With CPM:

```cmake
CPMAddPackage("gh:eyalamirmusic/MakeASound#main")
target_link_libraries(MyApp PRIVATE MakeASound)
```

Or as a subdirectory:

```cmake
add_subdirectory(MakeASound)
target_link_libraries(MyApp PRIVATE MakeASound)
```

Either way the apps and tests are skipped, since they only build for a top-level MakeASound. `Lib/` is the include root, and `<MakeASound/MakeASound.h>` is the single public header.

## Audio

`getDefaultConfig()` fills in only the sides the machine actually has — asking a mic-less desktop for a duplex stream fails the whole open, so the input stays unset rather than taking the output down with it.

```cpp
#include <MakeASound/MakeASound.h>
#include <cmath>
#include <numbers>

namespace MS = MakeASound;

auto manager = MS::DeviceManager {};
auto config = manager.getDefaultConfig();
auto phase = 0.f;

auto error = manager.start(
    config,
    [&](MS::AudioCallbackInfo& info)
    {
        auto delta = 440.f / static_cast<float>(info.sampleRate);

        for (auto i = 0; i < info.numSamples; ++i)
        {
            auto value = std::sin(phase * 2.f * std::numbers::pi_v<float>) * 0.1f;

            for (auto channel: info.getOutput())
                channel[i] = value;

            phase += delta;

            if (phase >= 1.f)
                phase -= 1.f;
        }
    });

if (error != MS::Error::NoError)
    std::cout << MS::getErrorMessage(error) << '\n';
```

`Buffer` is a non-owning planar view and iterates over its channels; a `Channel` is an `EA::Span<float>`, so range-for, the standard algorithms and EA's own `fill`/`copyFrom`/`mixFrom` all work on it. Sizes and indices are `int` everywhere, so call sites never convert to `size_t`.

### Picking a device

```cpp
for (auto& device: manager.getDevices())
    std::cout << device.id << ": " << device.name
              << " (" << MS::getBackendName(device.backend) << ")\n";

config.output = MS::StreamParameters {device, false};      // all channels
config.output = MS::StreamParameters {device, false, 2, 2}; // channels 3-4
config.sampleRate = 48000;
config.maxBlockSize = 256;

manager.setConfig(config);
```

Device ids are handed out per audio API, so a `DeviceInfo` only means something to the manager that enumerated it. `setBackend` therefore stops the stream and drops the config; follow it with a fresh `getDefaultConfig()`.

`getSupportedBlockSizes(device)` and `getCurrentSampleRate(device)` answer what a device can and does run at without opening it — from Core Audio on macOS, AVAudioSession on iOS, and a conservative 64..2048 fallback elsewhere.

### Reacting to the device

```cpp
manager.setNotificationCallback([](MS::DeviceNotification n) { log(n); });
manager.setAutoRecover(false); // own the "device lost" decision yourself
```

The notification callback runs on an OS audio thread, sometimes while recovery holds the device — set it before `start()` and don't call back into `DeviceManager` from it.

Inside the callback, `info.dirty` is raised whenever the stream shape (channels, sample rate, block size) differs from the previous block, and on the first callback after a reroute or interruption. It is the signal to reallocate working buffers and reset any state that depends on the rate.

## MIDI

Callback mode fires on RtMidi's own thread:

```cpp
namespace MIDI = MakeASound::MIDI;

auto midi = MS::MidiManager {};

for (auto& port: midi.getInputPorts())
    std::cout << port.id << ": " << port.name << '\n';

midi.openInput(portId,
               [](const MS::MidiMessage& message)
               {
                   auto size = static_cast<int>(message.bytes.size());

                   if (auto event = MIDI::convertMidi(message.bytes.data(), size))
                       std::cout << MIDI::toString(*event) << '\n';
               });

midi.sendMessage(MIDI::Event::noteOn(0, 60, 100.f / 127.f));
```

Queue mode accumulates events instead, so an audio callback can drain them with sample offsets already resolved:

```cpp
auto sync = MS::MidiBlockSync {};
midi.openInput(portId); // no callback: queue mode

manager.start(config,
              [&](MS::AudioCallbackInfo& info)
              {
                  if (info.dirty)
                      sync.reset();

                  sync.drainForBlock(midi, info.numSamples, info.sampleRate);

                  for (auto& in: sync.events())
                      synth.handle(in.event); // sampleOffset is within this block
              });
```

Events land one block late — the only way to keep offsets non-negative when MIDI arrives on its own thread.

`openVirtualInput` / `openVirtualOutput` create ports other apps can connect to; they exist on Core MIDI, ALSA and JACK, and throw on Windows.

## The probe app

`Apps/AudioProbe` is the example that runs everywhere the library does — macOS,
Windows and iOS — and it is deliberately two things at once.

It is a **visualizer**: a tone generator feeding MakeASound's own `SPSCQueue`, an
FFT on the render thread, and a spectrum drawn by a shader written in
[eacp](https://github.com/eyalamirmusic/eacp)'s GPU EDSL. The controls beneath it
are eacp's widget tier fed by `MakeASound::UIDeviceManager` — the device, rate and
block-size dropdowns are the library's own `UI::DropdownInfo` values.

It is also a **gap report**. Twelve probes ask what a caller should be able to
expect of this library and record what it actually does on the machine it is
running on. Each row shows what it expected, what it got, and whether that is a
pass, a gap, or a question nothing has answered yet.

```bash
./build/Apps/AudioProbe/AudioProbe            # the window
./build/Apps/AudioProbe/AudioProbe --strict   # log every gap, exit with the count
./build/Apps/AudioProbe/AudioProbe --assert   # trip an assertion on the first gap
```

For iOS, configure for the simulator and install the bundle:

```bash
cmake -S . -B build-ios -G Ninja -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build-ios --target AudioProbe

xcrun simctl boot "iPhone 17 Pro"
xcrun simctl install booted build-ios/Apps/AudioProbe/AudioProbe.app
xcrun simctl launch --console booted com.eyalamir.makeasound.audioprobe --strict
```

### What it finds today

Two gaps on macOS, seven on iOS. These are findings about MakeASound, not bugs in
the probe:

| probe | what it reports |
| --- | --- |
| `session/owned-by-app` | constructing a `DeviceManager` moves the AVAudioSession from SoloAmbient to PlayAndRecord and activates it, and nothing in the API chooses either |
| `session/microphone-cost` | that category is why a playback-only app still needs `NSMicrophoneUsageDescription` in its bundle |
| `config/playback-only` | `getDefaultConfig()` claims the built-in microphone; a playback app has to know to call `config.input.reset()` |
| `devices/default-flag` | iOS enumeration never sets `isDefaultOutput`, so the manager falls back to the first device with outputs |
| `devices/rate-choices` | the default output offers one sample rate, the session's current one, so a rate picker has nothing to show |
| `stream/negotiated-block-size` | there is no `getStreamBlockSize()` beside `getStreamSampleRate()`: only `AudioCallbackInfo` says what the device runs |
| `midi/virtual-port-errors` | `openVirtualOutput` throws on iOS ("error creating OS-X virtual MIDI source"), where the audio façade would return an `Error` |

Three more are live checks with nothing to report until the device does something
on its own — a reroute, an OS-initiated stop, a notification from an audio thread
— and the rest pass.

## Layout

```
Lib/MakeASound/
  MakeASound.h      umbrella public header
  Audio/            Buffer and Channel, non-owning planar views
  Devices/          DeviceInfo data types, DeviceManager façade, device queries
  MIDI/             typed events, port info, block sync, MidiManager façade
  Realtime/         SPSCQueue
  UI/               dropdown/toggle-list helpers for the demo apps
  Common/           EA type re-exports and audio-thread-safe algorithms
  MiniAudio/        audio backend (hidden)
  RTMidi/           MIDI backend (hidden)
Apps/               AudioProbe (GPU/UI, iOS too), Example, MidiDemo (CLI),
                    Demo, Synth (web UI)
Tests/              NanoTest suites
```

Data structs opt into JSON reflection in place via `MIRO_REFLECT(...)`, so a `StreamConfig` round-trips through [Miro](https://github.com/eyalamirmusic/Miro) without any extra code:

```cpp
Miro::logJSON(manager.getDefaultConfig());
```

## Dependencies

Fetched automatically: [miniaudio](https://github.com/mackron/miniaudio), [RtMidi](https://github.com/thestk/rtmidi), [Miro](https://github.com/eyalamirmusic/Miro), `ea_data_structures`, plus [eacp](https://github.com/eyalamirmusic/eacp) and [NanoTest](https://github.com/eyalamirmusic/NanoTest) for the apps and tests. Miro is linked `PUBLIC` (it leaks through the reflected data structs); miniaudio and RtMidi are `PRIVATE`, fully hidden behind the façades.
