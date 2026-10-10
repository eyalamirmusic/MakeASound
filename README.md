# MakeASound

A C++20 static library that gives platform-agnostic access to audio devices and MIDI ports.

Two façades make up the whole public surface:

- **`MakeASound::DeviceManager`** — audio device enumeration and streaming, backed by [miniaudio](https://github.com/mackron/miniaudio).
- **`MakeASound::MidiManager`** — MIDI input/output ports, backed by the platform's own MIDI API: Core MIDI on macOS and iOS, WinMM on Windows, the ALSA sequencer on Linux.

Each façade hides its backend behind a pimpl, so no backend type ever leaks into a header you include.

On top of them sits an optional second target, `MakeASoundPlugin`: the SDK-free core of a write-once plugin framework (see [Plugins](#plugins)), and its first format, a standalone app (see [Running a plugin standalone](#running-a-plugin-standalone)).

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
- **One `Buffer` type, owning or referring.** Planar, move-only, sliced by sample range or channel subset into further `Buffer`s over the same channel table, so a sub-block is free and never allocates. The backend de-interleaves on the way in and re-interleaves on the way out, so a callback only ever sees channel-major data.
- **Errors, not exceptions.** A machine with no device, or with one that is busy, is an ordinary desktop state: `start`/`setConfig` return an `Error` and `getErrorMessage` turns it into something a user can read.
- **Automatic recovery.** A device stopped by the OS — sample-rate change, unplug, reclaim — is re-opened on a worker thread; a `dirty` flag on the callback tells you when to re-derive anything you cached.
- **Typed MIDI.** `MIDI::Event` is a variant over note on/off, CC, pitch bend, aftertouch, program change and short SysEx, with allocation-free conversion to and from raw bytes.
- **Block-aligned MIDI.** `MidiBlockSync` resolves arrival times into sample offsets inside the current audio block.
- **A `Processor` run by an `Engine`.** Write `prepare`/`process`/`reset` against a `ProcessContext` of buses, and `Engine` wires it to a device stream and the open MIDI ports: the main buses refer straight into the callback's channels, MIDI arrives sorted by sample offset, and a device that cannot feed a bus channel is stood in for. The same `Processor` is what the plugin formats will host.
- **A plugin core.** A `Plugin` is a `Processor` with a parameter group and a versioned JSON state; `PluginWrapper` is the per-block pipeline every format adapter shares.
- **A standalone format.** One CMake call turns a plugin into an app with a generic parameter editor, device and MIDI pickers, a computer MIDI keyboard and its settings and state kept across launches.
- **Real-time safe pieces.** `SPSCQueue`, `MidiManager::drainMessages`, `MIDI::Buffer::sortByOffset`, `Smoother`, `ScopedNoDenormals` and the `Algorithms` helpers neither allocate nor lock.

## Requirements

- CMake 3.31+ and a C++20 compiler.
- macOS 11+, iOS 15+, Windows (x64 and ARM64), or Linux. CI builds macOS universal (arm64 + x86_64), iOS device + simulator, Windows with MSVC and clang-cl on both architectures, and Linux with GCC and Clang.
- Linux additionally needs the ALSA development headers: `sudo apt-get install libasound2-dev`. The library, tests and console demos build there; the GUI apps (`AudioProbe`, `Demo`, `Synth`) and the standalone plugin format are skipped, since eacp draws on macOS, Windows and iOS only.

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
| `MAKEASOUND_BUILD_PLUGIN` | `ON` top-level, `OFF` as a dependency | Build `MakeASoundPlugin`, the plugin core, and its tests. Fetches eacp. Where eacp builds its UI tier, also the generic editor; on a desktop (macOS, Windows), also the standalone format. |
| `MAKEASOUND_BUILD_EXAMPLES` | `ON` | Build the example plugins in `Plugins/` (top-level builds with the plugin core only). |
| `MAKEASOUND_UNITY_BUILD` | `OFF` | Jumbo build of the library. |

To develop against a local checkout of a dependency instead of the fetched copy, pass e.g. `-DCPM_Miniaudio_SOURCE=/path/to/miniaudio` at configure time. (No MIDI library is fetched at all — every backend is the platform's own.)

### Tests

```bash
ctest --test-dir build --output-on-failure
```

Two of the suites are about allocation rather than behaviour: they link
[ScopedMemoryAllocations](https://github.com/eyalamirmusic/ScopedMemoryAllocations),
which interposes `malloc`/`free` and `new`/`delete` for the test binary only, and
assert that the real-time paths never reach the allocator. `AllocationTests.cpp`
covers what can be called directly — MIDI encode/decode, the block buffers, the
`Buffer` slices and operations, the façade calls a host makes with nothing open.
`RealtimeThreadAllocationTests.cpp` covers the threads we do not own: it raises the
(thread-local) ban from inside a live audio callback and from inside the platform's MIDI input
thread, so a steady-state block and a delivered MIDI message are measured end to
end. Those need a playback device and a virtual MIDI port, and measure nothing
rather than failing where the platform has neither. `PluginAllocationTests.cpp`
does the same for the plugin core: parameter values, host-id lookup, the realtime
swap and a whole `PluginWrapper` block; `StandaloneAllocationTests.cpp` a
standalone block on `Engine`.

Interposition needs `dlsym(RTLD_NEXT, ...)`, so these files are added to the test
target on Apple and Linux only, rather than reporting zero allocations elsewhere
because nothing was watching.

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

Ask for the direction you want: `getDefaultOutputConfig()`, `getDefaultInputConfig()` or `getDefaultDuplexConfig()`. There is no one call that guesses, because a config that claims a capture side an app never wanted costs it the microphone permission on macOS and iOS both. Each fills in only the sides the machine actually has — asking a mic-less desktop for a duplex stream fails the whole open, so the input stays unset rather than taking the output down with it.

```cpp
#include <MakeASound/MakeASound.h>
#include <cmath>
#include <numbers>

namespace MS = MakeASound;

auto manager = MS::DeviceManager {};
auto config = manager.getDefaultOutputConfig();
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

`getOutput()` hands back a `Buffer` that refers to the backend's scratch; the same type, constructed with a channel count and a sample count, owns its storage. A `Buffer` iterates over its channels; a `Channel` is an `EA::Span<float>`, so range-for, the standard algorithms and EA's own `fill`/`copyFrom`/`mixFrom` all work on it. `getSubBuffer(start, length)` and `getChannelSubset(first, count)` return further `Buffer`s over the same channels, which is how a block is split around MIDI events or a bus is carved out of a wider one:

```cpp
auto output = info.getOutput();
output.getSubBuffer(0, event.sampleOffset).clear();
output.getChannelSubset(2, 2).applyGain(0.5f);
```

`Buffer` is move-only. A deep copy is `Buffer::copyOf(source)`, and `setSize` keeps its capacity so a buffer sized in a prepare step never allocates again for equal or smaller shapes. Sizes and indices are `int` everywhere, so call sites never convert to `size_t`.

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

`getSupportedBlockSizes(device)`, `getCurrentSampleRate(device)`, `getRouteLatency(device, input)` and `getDefaultDeviceName(input)` answer what a device can do, does, costs and is chosen as, without opening it — from Core Audio on macOS, AVAudioSession on iOS, and conservative fallbacks elsewhere. `getStreamLatency()` already includes the route's own delay, so it is the number to compensate for.

### Reacting to the device

```cpp
for (auto notification: manager.drainNotifications())  // from a UI timer
    updateUI(notification);

manager.setAutoRecover(false); // own the "device lost" decision yourself
```

`drainNotifications()` hands back everything queued since the last call, on the thread that asks — so a UI reads it from a timer it already has instead of marshalling off an audio thread itself. Undrained notifications stop accumulating at 64.

`setNotificationCallback` is the same news delivered immediately instead, and it runs wherever the OS raised it: on an OS audio thread, sometimes while recovery holds the device. Set it before `start()` and don't call back into `DeviceManager` from it.

Inside the callback, `info.dirty` is raised whenever the stream shape (channels, sample rate, block size) differs from the previous block, and on the first callback after a reroute or interruption. It is the signal to reallocate working buffers and reset any state that depends on the rate.

### The audio session (iOS)

iOS gates what an app may do behind an `AVAudioSession`, and MakeASound never
configures one behind your back — constructing a `DeviceManager` leaves it exactly
where it was. The session is applied on each open, and **follows the stream**: the
category is `Playback` for an output-only config and `PlayAndRecord` for one with a
capture side, and the rate and block size the `StreamConfig` asked for are what
`setPreferredSampleRate:` and `setPreferredIOBufferDuration:` are asked for.

That is usually all an app needs. `setSessionConfig` overrides any of it — most
usefully the options that decide whether opening a stream stops the user's music:

```cpp
auto session = MS::SessionConfig {};
session.options.mixWithOthers = true;
session.category = MS::SessionCategory::Ambient;  // unset follows the stream

manager.setSessionConfig(session);
manager.start(manager.getDefaultOutputConfig(), callback);
```

`getSessionState()` reports what the route actually granted — category, channels,
rate, block size and the route's own latency. Off iOS `hasAudioSession()` is false,
every call is a no-op and `getSessionState().available` is false, so the same code
compiles and runs everywhere.

## MIDI

Callback mode fires on the platform's own MIDI thread:

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

`openVirtualInput` / `openVirtualOutput` create ports other apps can connect to; they exist on Core MIDI (iOS included) and ALSA. Where they do not — Windows, and the iOS simulator, which refuses them to a process with no bundle — `openVirtualOutput` returns an `Error` and `openVirtualInput` returns `nullopt`, as the audio side would. Nothing in the MIDI facade throws.

## Processors

`Processor` is the format-neutral unit of work: `getBusLayout()` says what buses it wants, `prepare` receives a `ProcessSpec` (rate, block size, layout), `process` gets a `ProcessContext` whose buffers refer to the host's memory, and `reset` drops state after a gap. `Engine` runs one on a device stream with the open MIDI inputs on its main MIDI bus:

```cpp
struct Gain : MS::Processor
{
    void prepare(const MS::ProcessSpec&) override {}

    void process(MS::ProcessContext& ctx) noexcept override
    {
        auto& out = ctx.mainOutput();
        out.copyFrom(ctx.mainInput());
        out.applyGain(0.5f);
    }
};

auto devices = MS::DeviceManager {};
auto midi = MS::MidiManager {};
auto engine = MS::Engine {devices, midi};
auto gain = Gain {};

engine.start(devices.getDefaultDuplexConfig(), gain);
```

`BusLayout::stereoInOut()` is the default layout; an instrument returns `BusLayout::instrument()` from `getBusLayout()` and reads `ctx.mainMidiIn()`, a `MIDI::Buffer` already sorted by offset, so a block is split around events with `mainOutput().getSubBuffer(from, length)`. A layout with no input bus never opens the capture side, so an instrument costs no microphone permission. `Apps/Synth` is the worked example: it runs `MakeASound::DSP::TestSynth` from the optional `MakeASoundDSP` library, a monophonic instrument with a choice of waveform, an attack/release envelope and a smoothed level, and `Plugins/Synth` plays the same synth as a plugin.

## Plugins

`MakeASoundPlugin` is a second static target, built when `MAKEASOUND_BUILD_PLUGIN` is on (the default in a top-level build; a project consuming MakeASound turns it on): the SDK-free core of a write-once plugin framework. The standalone app is the first format (below); VST3 and AU are the next stages in `plan.md`. A plugin is a `Processor` with a name, a parameter group and a state document, and a module describes the plugins it holds:

```cpp
#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace MS = MakeASound;

struct GainParams : MS::ParameterGroup
{
    GainParams() { add(gain, bypass); }

    MS::DecibelParam gain {"Gain", -60.f, 12.f, 0.f};
    MS::BoolParam bypass {"Bypass", false, {.bypass = true}};
};

struct GainPlugin : MS::StatePlugin<MS::State<GainParams>>
{
    std::string_view name() const override { return "Gain"; }

    void prepare(const MS::ProcessSpec&) override {}

    void process(MS::ProcessContext& ctx) noexcept override
    {
        if (!params.bypass.isOn())
            ctx.mainOutput().applyGain(params.gain.gain());
    }
};

namespace MakeASound
{
ModuleDescription describeModule()
{
    auto module = ModuleDescription {};
    module.vendor = "Me";
    module.manufacturerCode = "MeMe";
    module.plugins.add({.name = "Gain",
                        .pluginCode = "Gain",
                        .create = [] { return EA::makeOwned<GainPlugin>(); }});
    return module;
}
} // namespace MakeASound
```

```cmake
target_link_libraries(MyPlugin PRIVATE MakeASoundPlugin)
```

Parameters are declared in a `ParameterGroup` and registered with `add(...)` in its constructor, in the order the host lists them. A nested group takes a name per instance, so `OscParams osc1 {"Osc 1"}` gives its `attack` the id `"Osc 1/Attack"` and the name `"Osc 1 Attack"`; `add` also takes arrays and vectors of groups or parameters, each element under its own name. An id defaults to the name, and `{.id = "cutoff"}` on a parameter or a group pins it so a later rename keeps saved state and automation. The host id is a 31-bit hash of the full id rather than a position (VST3 reserves the ids above), so inserting a parameter in a later release moves nobody's automation; `{.hostId = ...}` pins one by hand. Values are atomics, read straight from `process`. `StatePlugin` saves and loads the group as JSON with a `version` field: choices by name, a missing key back to its default, and a `{.sessionOnly = true}` parameter only in a DAW session, never in a preset. A load assigns into the registered parameters and rebuilds nothing. `version` is what this build writes; the loaded document's is `loadedVersion`, for migrating in a `reflect` override. Parameters are not Miro fields: one in a `MIRO_REFLECT` list does not compile.

`PluginWrapper` is what a format adapter drives, one block at a time. By the time `process` runs, each output channel already holds its input (copied, or the same memory when the host processes in place), which is why the gain above only multiplies; after the block every bus is detached again. A host saving from its own thread gets the published snapshot without waiting on the message thread, and a load applies the parameters at once and the rest of the document on the message thread, unless a newer load overtook it. While the plugin's own editor holds a parameter in a gesture, host writes to it are dropped. Destroy the wrapper on the message thread. `Tests/PluginTests.cpp` drives it as a fake host.

## Running a plugin standalone

`Plugins/Gain` is a complete plugin. The header declares the parameters and the processor:

```cpp
#pragma once

#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace MakeASoundExamples
{
using namespace MakeASound;

struct GainParams : ParameterGroup
{
    GainParams() { add(gain); }

    DecibelParam gain {"Gain", -60.f, 12.f, 0.f};
};

struct GainPlugin : StatePlugin<State<GainParams>>
{
    std::string_view name() const override { return "Gain"; }

    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    Smoother gain;
};

} // namespace MakeASoundExamples
```

and the source implements it and describes the module:

```cpp
#include "GainPlugin.h"

namespace MakeASoundExamples
{

void GainPlugin::prepare(const ProcessSpec& spec)
{
    gain.setSampleRate(spec.sampleRate);
    gain.setRampTime(0.02f);
    reset();
}

void GainPlugin::reset() noexcept { gain.reset(params.gain.gain()); }

void GainPlugin::process(ProcessContext& context) noexcept
{
    gain.setTarget(params.gain.gain());
    gain.applyGain(context.mainOutput());
}

} // namespace MakeASoundExamples

namespace MakeASound
{

ModuleDescription describeModule()
{
    auto module = ModuleDescription {};
    module.vendor = "MakeASound";
    module.manufacturerCode = "MkAS";
    module.plugins.add(
        {.name = "Gain",
         .category = Category::Effect,
         .pluginCode = "Gain",
         .create = [] { return EA::makeOwned<MakeASoundExamples::GainPlugin>(); }});
    return module;
}

} // namespace MakeASound
```

One CMake call builds it:

```cmake
makeasound_add_plugin(Gain
        FORMATS Standalone
        OUTPUT_NAME "MakeASound Gain"
        SOURCES GainPlugin.cpp)
```

That makes `Gain`, a static library holding the plugin, and `Gain-Standalone`, the app (an ad-hoc signed `.app` bundle on macOS). `BUNDLE_ID` and `COMPANY` are optional, and VST3 and AU will be further `FORMATS` on the same call. The function comes with MakeASound, so a project consuming it through CPM with `MAKEASOUND_BUILD_PLUGIN` on calls it the same way.

```bash
open "./build/Plugins/Gain/MakeASound Gain.app"
open "./build/Plugins/Synth/MakeASound Synth.app"   # the instrument example
```

The app hosts the module's first plugin, run by `Engine`:

- **The editor window** shows the plugin's own `Editor` when `createEditor()` returns one, and otherwise a generic page: a slider, a dropdown or a checkbox per parameter, with its value text, kept in step with the parameters while it is open. Both are eacp GPU widgets, so neither needs npm or a webview.
- **Audio / MIDI Settings…** (Cmd+,) picks the output and input devices and channels, the sample rate, the block size, the MIDI inputs and, for a plugin with a MIDI output bus, the port it sends to. Rows a layout cannot use are hidden, and the lists follow devices and ports as they come and go. An instrument opens no capture side, so it asks for no microphone.
- **The computer MIDI keyboard** (Cmd+K, on by default for a plugin with a MIDI input): `A W S E D F T G Y H U J K O L P ;` play an octave and a half from middle C, `Z` and `X` shift it an octave. Notes held when the window loses focus are released.
- **Settings and state survive a relaunch.** Devices, ports and the plugin's session state are saved to `settings.json` under the platform's app-support directory, in `<vendor>/<plugin name>/`, on every change and on quit. Devices and ports are stored by name and found again on launch, and a device that is gone falls back to the default. `Reset Plugin to Defaults` and `Reset Audio / MIDI Settings` are in the app menu.

A plugin's MIDI output goes to the chosen port from a sender thread of its own, since sending is not safe on the audio thread. `Plugins/Synth` is `TestSynth` from `MakeASoundDSP` with its settings on parameters: a monophonic instrument that plays from a hardware port and the typing keyboard.

## The probe app

`Apps/AudioProbe` is the example that runs everywhere eacp draws — macOS,
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

Nothing, on either platform — which is the point of having it. It found nine gaps
when it was written; `gaps.md` records each one, what changed, and what is still
open.

```
macOS   0 gaps, 7 pass, 3 waiting, 3 n/a
iOS     0 gaps, 10 pass, 3 waiting, 0 n/a
```

Three rows stay `waiting` on both: an OS-initiated stop, a route change and a
notification raised off the main thread are all still gaps, and all three need
hardware no simulator provides.

## Layout

```
Lib/MakeASound/
  MakeASound.h      umbrella public header
  Audio/            Buffer, Channel, and the Processor / ProcessContext / BusLayout vocabulary
  Devices/          DeviceInfo data types, DeviceManager façade, Engine, device queries
  MIDI/             typed events, port info, block sync, MidiManager façade
  Realtime/         SPSCQueue, SpinLock, ScopedNoDenormals, Smoother
  Plugin/           MakeASoundPlugin.h, the plugin core's umbrella header
    Parameters/     Parameter, FloatParam, ChoiceParam, BoolParam, ParameterGroup, ParameterList
    State/          StateContext, State
    Core/           Description, Editor, HostEditListener, Plugin, StatePlugin
    Host/           PluginWrapper, the per-block pipeline every format adapter shares
    Realtime/       MessageThread, RealtimeSwap
    UI/             MakeASoundPluginUI: the generic parameter editor
    Standalone/     MakeASoundStandalone: the standalone app format
  DSP/              MakeASoundDSP, optional: TestSynth, the instrument the Synth app and plugin share
  UI/               dropdown/toggle-list helpers for the demo apps
  Common/           EA type re-exports and audio-thread-safe algorithms
  MiniAudio/        audio backend (hidden)
  CoreMIDI/         MIDI backend, Apple (hidden)
  WinMIDI/          MIDI backend, Windows WinMM (hidden)
  ALSA/             MIDI backend, Linux sequencer (hidden)
Apps/               AudioProbe (GPU/UI, iOS too), Example, MidiDemo (CLI),
                    Demo, Synth (web UI)
Plugins/            Gain, Synth: plugins built with makeasound_add_plugin
CMake/              CPM, the Find modules, MakeASoundPlugin.cmake, ExternalFolders.cmake
Tests/              NanoTest suites
gaps.md             what the probe found, what was fixed, what is open
```

Data structs opt into JSON reflection in place via `MIRO_REFLECT(...)`, so a `StreamConfig` round-trips through [Miro](https://github.com/eyalamirmusic/Miro) without any extra code:

```cpp
Miro::logJSON(manager.getDefaultConfig());
```

## License

MIT; see [LICENSE](LICENSE). Every dependency is MIT or more permissive, and the
VST3 SDK vendored under `ThirdParty/VST3_SDK` is MIT as of 3.8.0, its notices kept
alongside. A plugin built on MakeASound ships those notices with it.

## Dependencies

Fetched automatically: [miniaudio](https://github.com/mackron/miniaudio), [Miro](https://github.com/eyalamirmusic/Miro), `ea_data_structures`, plus [eacp](https://github.com/eyalamirmusic/eacp) for the apps and the plugin core and [NanoTest](https://github.com/eyalamirmusic/NanoTest) + [ScopedMemoryAllocations](https://github.com/eyalamirmusic/ScopedMemoryAllocations) for the tests. No MIDI library is fetched: Core MIDI, WinMM and the ALSA sequencer come with the platform. Miro is linked `PUBLIC` (it leaks through the reflected data structs); miniaudio and whichever MIDI library the platform selected are `PRIVATE`, fully hidden behind the façades. `MakeASoundPlugin` links `MakeASound` `PUBLIC` and `eacp-core` `PRIVATE`, and none of its headers includes eacp; `MakeASoundPluginUI` adds `eacp-ui` and `MakeASoundStandalone` `eacp-graphics`, both `PUBLIC`.
