# MakeASound

A C++20 static library that gives platform-agnostic access to audio devices and MIDI ports.

Two façades make up the whole public surface:

- **`MakeASound::DeviceManager`** — audio device enumeration and streaming, backed by [miniaudio](https://github.com/mackron/miniaudio).
- **`MakeASound::MidiManager`** — MIDI input/output ports, backed by the platform's own MIDI API: Core MIDI on macOS and iOS, WinMM on Windows, the ALSA sequencer on Linux.

Each façade hides its backend behind a pimpl, so no backend type ever leaks into a header you include.

Beneath them is `MakeASoundCore` (alias `MakeASound::Core`): the audio and MIDI vocabulary and the processing types (`Buffer`, `ReadOnlyBuffer`, `BusLayout`, `Playhead`, `ProcessSpec`, `ProcessContext`, `Processor`, MIDI events and `MidiParser`, the realtime helpers), with no device backend. It links only Miro, and `<MakeASound/MakeASoundCore.h>` is its umbrella header. The `MakeASound` target links it `PUBLIC`, so linking `MakeASound` gives you both. Code that only processes audio (DSP, a plugin) links the core alone, and a project that wants nothing else sets `MAKEASOUND_BUILD_DEVICES` off, so miniaudio is never fetched.

On top of them sits an optional second target, `MakeASoundPlugin`: the SDK-free core of a write-once plugin framework (see [Plugins](#plugins)), and its formats: a standalone app (see [Running a plugin standalone](#running-a-plugin-standalone)), VST3 (see [VST3](#vst3)), on macOS AU (see [AU](#au)), on iOS AUv3 (see [AUv3 on iOS](#auv3-on-ios)), and on macOS and Windows x64, when asked for, AAX (see [AAX](#aax)).

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
- **A standalone format.** One CMake call turns a plugin into an app with a generic parameter editor, device and MIDI pickers, a computer MIDI keyboard and its settings and state kept across launches, on macOS, Windows and iOS. The host underneath has no window, so a plugin can bring an app of its own instead.
- **Editors that say how they size.** A plugin's `Editor` states its minimum, maximum and aspect ratio and can ask its host for a new size; every format honours it, and a host without a resize grip of its own gets one drawn by the editor. A plugin with no editor gets a generic one.
- **A VST3 format.** The same CMake call builds a `.vst3` bundle a DAW loads, with the plugin's editor or the generic one, sample-accurate automation, MIDI CC through hidden mapped parameters (instruments by default, any plugin that asks) and the plugin's state in the session; both examples pass pluginval at strictness 10.
- **An AU format.** On macOS the same call also builds a `.component`, an Audio Unit v2 with the plugin's editor or the generic one, sample-accurate MIDI in and out, the host's bypass button and the plugin's state in the session; both examples pass `auval -strict`.
- **An AUv3 format on iOS.** The same call puts an Audio Unit extension inside the iOS app, so AUM, GarageBand and every other iOS host find the plugin once the app is installed, with its editor in the host's window.
- **An AAX format, opt-in.** On macOS and Windows x64 with `-DMAKEASOUND_BUILD_AAX=ON` the same call also builds an `.aaxplugin` for Pro Tools, with the plugin's editor or the generic one, automation, MIDI in and out, the plugin's state in the session and its bypass parameter as the master bypass; both examples pass Avid's AAX Validator. Avid's SDK is fetched only under that flag and has its own licence terms (see [AAX](#aax)).
- **Real-time safe pieces.** `SPSCQueue`, `MidiManager::drainMessages`, `MIDI::Buffer::sortByOffset`, `Smoother`, `ScopedNoDenormals` and the `Algorithms` helpers neither allocate nor lock.

## Requirements

- CMake 3.31+ and a C++20 compiler.
- macOS 11+, iOS 15+, Windows (x64 and ARM64), or Linux. CI builds macOS universal (arm64 + x86_64), iOS device + simulator (Ninja, and the device again with Xcode), Windows with MSVC and clang-cl on both architectures, and Linux with GCC and Clang.
- Linux additionally needs the ALSA development headers: `sudo apt-get install libasound2-dev`. A build with the plugin core also configures eacp's Linux graphics tier, which wants its Wayland, xcb, xkbcommon, libdecor, FreeType, HarfBuzz and fontconfig dev packages and libcurl (the apt list in `.github/workflows/ci.yml`). The library, tests and console demos build there; the GUI apps (`AudioProbe`, `Demo`, `Synth`) and the standalone plugin format are skipped, since eacp draws on macOS, Windows and iOS only. The VST3 bundles build there, embedding their editor in the host's X11 window where eacp's UI tier builds.
- Node and npm, for the React demo apps (`Apps/Demo`, `Apps/Synth`) only: eacp's webview build runs `npm install` and `vite build` for them. `-DMAKEASOUND_BUILD_APPS=OFF` needs neither; the library, the plugin formats and the example plugins never need Node.

Dependencies are fetched by [CPM.cmake](CMake/CPM.cmake) on the first configure — nothing to install by hand.

## Build

```bash
cmake -S . -B build -G Ninja   # -DMAKEASOUND_INSTALL_PLUGINS=OFF to skip copying plug-ins into the plug-in folder
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
| `MAKEASOUND_BUILD_DEVICES` | `ON` | Build `MakeASound`, the device library over `MakeASoundCore`, and fetch miniaudio. Off, only the core is built: no miniaudio, no platform audio or MIDI framework, and no apps, standalone format or main test suite (`MakeASoundCoreTests` still builds). |
| `MAKEASOUND_BUILD_APPS` | `ON` | Build the example/demo apps (top-level builds only). |
| `MAKEASOUND_BUILD_TESTS` | `ON` | Build the unit tests (top-level builds only). |
| `MAKEASOUND_BUILD_PLUGIN` | `ON` top-level, `OFF` as a dependency | Build `MakeASoundPlugin`, the plugin core, and its tests. Fetches eacp. Where eacp builds its UI tier, also the generic editor; on macOS, Windows and iOS, also the standalone format's app (the windowless `StandaloneHost` builds everywhere). |
| `MAKEASOUND_BUILD_HOSTING` | `ON` top-level, `OFF` as a dependency | Build `MakeASoundHosting`, scanning third-party VST3 plugins and (on macOS) Audio Units, in worker processes, into a plugin database, loading and running them, and their editors; `Apps/PluginScanner` and the example host `Apps/PluginHost`. Needs the plugin core; macOS and Windows for now. See [Docs/Hosting.md](Docs/Hosting.md) and [Docs/PluginScanning.md](Docs/PluginScanning.md). |
| `MAKEASOUND_BUILD_EXAMPLES` | `ON` | Build the example plugins in `Plugins/` (top-level builds with the plugin core only). |
| `MAKEASOUND_INSTALL_PLUGINS` | `ON` | After each plug-in bundle builds, copy it into the user's plug-in folder (`~/Library/Audio/Plug-Ins/VST3`, `%LOCALAPPDATA%\Programs\Common\VST3`, `~/.vst3`; a `.component` into `~/Library/Audio/Plug-Ins/Components`; an `.aaxplugin` into `/Library/Application Support/Avid/Audio/Plug-Ins` or `C:\Program Files\Common Files\Avid\Audio\Plug-Ins`, which usually needs admin rights). A failed copy is a warning. |
| `MAKEASOUND_IOS_TEAM` | empty | The Apple development team an iOS tree signs its apps and AUv3 extensions with under the Xcode generator. Empty: ad hoc for the simulator, unsigned for a device. |
| `MAKEASOUND_DESCRIBE_CMAKE_ARGS` | empty | Extra configure arguments for an iOS tree's macOS sub-build that writes the AUv3 plists. |
| `MAKEASOUND_BUILD_AAX` | `OFF` | Fetch Avid's AAX SDK and build the AAX format (macOS and Windows x64, with the plugin core). Off, no Avid code is fetched or compiled. See [AAX](#aax) for the licence this puts on an AAX binary. |
| `MAKEASOUND_UNITY_BUILD` | `OFF` | Jumbo build of the library and its own targets. |
| `MAKEASOUND_CI_BUILD` | `OFF` | What CI configures with: unity builds here and in every dependency, plus eacp's precompiled headers. |

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

The hosting suites load only plugins this tree builds (`Tests/HostFixtures`, never
installed) and Apple's built-in Audio Units, never a third-party plugin, and pin the
plugin database's schema, which is append-only, against a file Tamber Plug wrote.

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

For the vocabulary alone, with no device backend fetched or linked:

```cmake
CPMAddPackage(NAME MakeASound GITHUB_REPOSITORY eyalamirmusic/MakeASound GIT_TAG main
        OPTIONS "MAKEASOUND_BUILD_DEVICES OFF")
target_link_libraries(MyDSP PRIVATE MakeASound::Core)   # <MakeASound/MakeASoundCore.h>
```

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

`ReadOnlyBuffer` refers to a `const float* const*` table, such as a host's inputs, and hands it out only as a `const Buffer&`. `Buffer` is move-only. A deep copy is `Buffer::copyOf(source)`, and `setSize` keeps its capacity so a buffer sized in a prepare step never allocates again for equal or smaller shapes. Sizes and indices are `int` everywhere, so call sites never convert to `size_t`.

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

`Processor` is the format-neutral unit of work: `getBusLayout()` says what buses it wants, `prepare` receives a `ProcessSpec` (rate, block size, layout), `process` gets a `ProcessContext` whose buffers refer to the host's memory, and `reset` drops state after a gap. `ProcessSpec::sampleRate` is a `double`: a plugin gets exactly the rate its host said, which need not be whole (a video pull-down such as 44055.94). `roundedSampleRate()` (or `roundSampleRate(double)`, `std::lround`) is for code that needs a whole rate, such as a table sized per second. Device rates stay `int`, since hardware clocks are whole, and become the spec's `double` exactly. `Engine` runs a processor on a device stream with the open MIDI inputs on its main MIDI bus:

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

`MakeASoundPlugin` is a second static target, built when `MAKEASOUND_BUILD_PLUGIN` is on (the default in a top-level build; a project consuming MakeASound turns it on): the SDK-free core of a write-once plugin framework. The standalone app, VST3, AU, AUv3 and AAX are its formats (below); CLAP is a later stage in `plan.md`. A plugin is a `Processor` with a name, a parameter group and a state document, and a module describes the plugins it holds:

```cpp
#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace MS = MakeASound;

struct GainParams : MS::ParameterGroup
{
    GainParams() { add(gain, bypass); }

    MS::DecibelParam gain {"Gain", -60.f, 12.f, 0.f, {.versionHint = 1}};
    MS::BoolParam bypass {"Bypass", false, {.bypass = true, .versionHint = 1}};
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

Parameters are declared in a `ParameterGroup` and registered with `add(...)` in its constructor, in the order the host lists them. A nested group takes a name per instance, so `OscParams osc1 {"Osc 1"}` gives its `attack` the id `"Osc 1/Attack"` and the name `"Osc 1 Attack"`; `add` also takes arrays and vectors of groups or parameters, each element under its own name. A parent can name a child instead with `addChild("Osc 1", osc1, {.id = "Osc1"})`, which is how one group type is mounted several times, an array's elements included. An id defaults to the name, and `{.id = "cutoff"}` on a parameter or a group pins it so a later rename keeps saved state and automation. An id may hold a `/` itself (a Tamber Plug port's `"Dry/Wet"`): its path doubles it, `"Dry//Wet"` at the root or `"Mix/Dry//Wet"` in a group `Mix`, so a path never confuses it with a group `Dry` holding `Wet`, and `splitIdPath` reads a path back into its ids (`Parameters/IdPath.h`). Saved state is nested objects keyed by each id as written, so the document's key stays `"Dry/Wet"`, and a path with no doubled `/` is exactly what it was. An id that begins or ends with `/` cannot be delimited and is reported by `identityProblems()`. The host id is a 31-bit hash (FNV-1a) of the full id path rather than a position (VST3 reserves the ids above), so inserting a parameter in a later release moves nobody's automation; `{.hostId = ...}` pins one by hand. **Pin before you regroup:** because the path includes every enclosing group, moving a parameter into, out of or between groups changes both its state key and its host id, breaking every saved session and automation lane that used it, unless its `id` and `hostId` were pinned first. Each parameter also takes `{.versionHint = n}`, the release that added it, which orders an AU's parameter list (see [AU](#au)). A plugin ported from another framework can instead keep every id its earlier build gave hosts by naming a compatibility profile on its `PluginDescription`. `.idScheme = MS::IdScheme::JUCE` (or `JUCELegacyIndex`), with `.juce` (JUCE's identity settings) and on each parameter `{.hostKey = "..."}`, its JUCE `ParameterID`, which defaults to the leaf id, names it as its JUCE build did. `MS::IdScheme::TamberPlug` keeps a Tamber Plug build's: each parameter's flat index among every leaf (sparse where a leaf is hidden), Plug's AU list, values and flags, and Plug's AU sessions loading as native documents with no `loadLegacy`. `IdScheme::Native` is the default (`IdScheme::MakeASound` is its deprecated old name). A profile resolves into an `IdentityPolicy` (`policyFor`, `identityPolicyFor`, `Core/IdentityPolicy.h`), the per-concern rules every format adapter reads, and `.overrides` replaces one rule where a port needs it. Under an index profile (`JUCELegacyIndex`, `TamberPlug`) the first release that adds a parameter anywhere but the end sets `.overrides.indexedThroughVersion` to the port's `versionHint` (1, usually) and gives the new parameter a higher one: it then takes a path hash like a native parameter's and moves no old index. `ParameterList::identityProblems()` lists every id clash in any build; a debug build asserts on it. A `FloatParam` takes a `FloatRange {min, max, interval, skew, symmetricSkew}`, JUCE's `NormalisableRange` with the same arithmetic: a skew below 1 gives the low end more travel, `FloatRange::withCentre(min, max, centre)` picks the skew that puts `centre` mid-travel, and an `interval` snaps every value (a host sees the grid as steps while the curve is linear). `HzParam` is centred at the geometric mean √(min·max), or linear when given a plain `FloatRange {min, max}` (a Tamber Plug port's `HzParam` keeps its linear mapping that way), and `IntParam` is a whole number with one step per value. `CommonParams.h` also has decibel, time, percent, bipolar (±100 %), semitone and MIDI note parameters (both continuous, rounded by `semitones()` and `note()`) and an octave choice. Values are atomics, read straight from `process`. `StatePlugin` saves and loads the group as JSON with a `version` field: choices by name, a missing key back to its default, and a `{.sessionOnly = true}` parameter only in a DAW session, never in a preset. A load assigns into the registered parameters and rebuilds nothing. `version` is what this build writes; the loaded document's is `loadedVersion`, for migrating in a `reflect` override. Parameters are not Miro fields: one in a `MIRO_REFLECT` list does not compile.

`PluginWrapper` is what a format adapter drives, one block at a time. By the time `process` runs, each output channel already holds its input (copied, or the same memory when the host processes in place), which is why the gain above only multiplies; after the block every bus is detached again. A host saving from its own thread gets the published snapshot without waiting on the message thread, and a load applies the parameters at once and the rest of the document on the message thread, unless a newer load overtook it. While the plugin's own editor holds a parameter in a gesture, host writes to it are dropped. Destroy the wrapper on the message thread. `Tests/PluginTests.cpp` drives it as a fake host.

### Editors

A plugin shows an `Editor`: one eacp view, an initial size and a size policy. `createEditor()` returns it; returning nothing gets the generic editor, a resizable list of every parameter.

```cpp
struct MyEditor : MS::Editor
{
    eacp::Graphics::View& view() override;   // built on first call

    MS::EditorSize initialSize() const override { return {600, 400}; }

    MS::EditorConstraints constraints() const override
    {
        return {.resizable = true,
                .minimum = {450, 300},
                .maximum = {1200, 800},
                .aspectRatio = {3, 2}};
    }
};

MS::OwningPointer<MS::Editor> MyPlugin::createEditor()
{
    return EA::makeOwned<MyEditor>();
}
```

Every format opens the editor at its initial size put through `constraints().constrain`, and puts every size the user drags to through the same rule, so a 3:2 editor stays 3:2 in the standalone window, a VST3 host and an AU view. An editor can also ask for a size itself: `requestResize({800, 533})` constrains it and hands it to the host, returning whether the host took it. Where the host's window has no grip of its own (an AU view in Logic), the generic editor draws a `ResizeGrip` in its corner; a custom editor adds one the same way when `ResizeGrip::isWantedBy(*this)`.

### Web editors

MakeASound has no web-editor support of its own. A plugin that wants a web page as its editor builds one directly on eacp's `WebView` (and its vite build helpers): an `Editor` whose `view()` returns the `WebView`, with the page talking to the plugin's parameters through whatever bridge the plugin binds. The framework only sees an `Editor`, so the size policy, `requestResize` and `ResizeGrip` above apply to it as to any other.

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
    GainParams() { add(gain, bypass); }

    // versionHint 1: the first release's parameters, ahead of any added later in
    // an AU's parameter list.
    DecibelParam gain {"Gain", -60.f, 12.f, 0.f, {.versionHint = 1}};
    BoolParam bypass {"Bypass", false, {.bypass = true, .versionHint = 1}};
};

struct GainPlugin : StatePlugin<State<GainParams>>
{
    std::string_view name() const override { return "Gain"; }

    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    float targetGain() const noexcept;

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

void GainPlugin::reset() noexcept { gain.reset(targetGain()); }

void GainPlugin::process(ProcessContext& context) noexcept
{
    gain.setTarget(targetGain());
    gain.applyGain(context.mainOutput());
}

float GainPlugin::targetGain() const noexcept
{
    return params.bypass.isOn() ? 1.f : params.gain.gain();
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
        FORMATS Standalone VST3 AU AUv3 AAX
        OUTPUT_NAME "MakeASound Gain"
        SOURCES GainPlugin.cpp)
```

That makes `Gain`, a static library holding the plugin, `Gain-Standalone`, the app (an ad-hoc signed `.app` bundle on macOS), `Gain-VST3`, the VST3 plug-in (see [VST3](#vst3)), on macOS `Gain-AU`, the Audio Unit (see [AU](#au)), on iOS `Gain-AUv3`, the Audio Unit extension inside the app (see [AUv3 on iOS](#auv3-on-ios)), and on macOS and Windows x64 with `MAKEASOUND_BUILD_AAX` on `Gain-AAX`, the Pro Tools plug-in (see [AAX](#aax)). `BUNDLE_ID`, `COMPANY` and `VERSION` (default `1.0.0`) are optional. A format the machine or the configuration cannot build, AU off macOS, AUv3 off iOS or AAX with the flag off or on Windows ARM64 for three, is skipped with a status line, so one list of formats serves every platform. The function comes with MakeASound, so a project consuming it through CPM with `MAKEASOUND_BUILD_PLUGIN` on calls it the same way.

**Bundle identifiers.** `BUNDLE_ID` (default `com.makeasound.<name>`, lowercased) is the app's identifier. Each plug-in bundle gets its own identifier, made from it with the format's suffix: `<BUNDLE_ID>.vst3`, `.component`, `.auv3` and `.aaxplugin`. They differ because the macOS Installer finds a package's components by bundle id. `VST3_BUNDLE_ID`, `AU_BUNDLE_ID`, `AUV3_BUNDLE_ID` and `AAX_BUNDLE_ID` each replace one format's whole identifier. They are for a plugin ported from another framework, whose installed bundles must keep the identifier they shipped with, so that an installer updates them in place:

```cmake
makeasound_add_plugin(Gain
        FORMATS VST3 AU
        BUNDLE_ID com.tamberplug.Gain
        AU_BUNDLE_ID com.tamberplug.Gain.au     # rather than com.tamberplug.Gain.component
        SOURCES GainPlugin.cpp)
```

An override sets the bundle's `CFBundleIdentifier`, and so the identifier its signature is made under. It changes nothing a host stores. The Objective-C class suffixes and the AAX effect ids still derive from `BUNDLE_ID`. An app extension's identifier must extend its app's, so `AUV3_BUNDLE_ID` must begin with `<BUNDLE_ID>.`, or the configure stops with an error. `Tests/BundleIds` checks how these resolve and, on macOS, what two fixtures' bundles were given.

```bash
open "./build/Plugins/Gain/MakeASound Gain.app"
open "./build/Plugins/Synth/MakeASound Synth.app"   # the instrument example
open "./build/Plugins/GainApp/MakeASound Gain App.app"   # Gain in an app of its own
```

The app hosts the module's first plugin, run by `Engine`:

- **The editor window** shows the plugin's own `Editor` when `createEditor()` returns one, and otherwise a generic page: a slider, a dropdown or a checkbox per parameter, with its value text, kept in step with the parameters while it is open. The window opens at the editor's constrained size and keeps every resize within its constraints, with no full screen for an editor locked to a ratio.
- **Audio / MIDI Settings…** (Cmd+,) picks the output and input devices and channels, the sample rate, the block size, the MIDI inputs and, for a plugin with a MIDI output bus, the port it sends to. Rows a layout cannot use are hidden, and the lists follow devices and ports as they come and go. An instrument opens no capture side, so it asks for no microphone.
- **The computer MIDI keyboard** (Cmd+K, on by default for a plugin with a MIDI input): `A W S E D F T G Y H U J K O L P ;` play an octave and a half from middle C, `Z` and `X` shift it an octave. Notes held when the window loses focus are released.
- **Settings and state survive a relaunch.** Devices, ports and the plugin's session state are saved to `settings.json` under the platform's app-support directory, in `<vendor>/<plugin name>/`, on every change and on quit. Devices and ports are stored by name and found again on launch, and a device that is gone falls back to the default. `Reset Plugin to Defaults` and `Reset Audio / MIDI Settings` are in the app menu.

On iOS the same target is one screen: the editor, fitted to the safe area, under a bar whose `Settings` button swaps it for the device and MIDI settings, with rows sized for a finger and the two resets at the bottom. The app keeps playing in the background (`UIBackgroundModes` `audio`). There is no typing keyboard there yet. Pass `NO_AUDIO_INPUT` for a plugin with no input bus, as the Synth does, and the app's plist carries no microphone string.

### A standalone app of your own

What runs the plugin, the devices, the MIDI ports, `Engine` and the settings file, is `StandaloneHost`, with no window in it. To replace the default app, hand `makeasound_add_plugin` your own app's sources; the bundle, plist and signing stay the same:

```cmake
makeasound_add_plugin(GainApp
        FORMATS Standalone
        OUTPUT_NAME "MakeASound Gain App"
        SOURCES ../Gain/GainPlugin.cpp
        STANDALONE_SOURCES GainApp.cpp)   # [STANDALONE_LIBRARIES <targets>...]
```

The app includes `<MakeASound/Plugin/Standalone/MakeASoundStandalone.h>`, holds a `StandaloneHost` as its first member and its UI after it, calls `host.start()` in its constructor, and defines `main()` as `return eacp::Apps::run<MyApp>();`. `SettingsPanel`, `GenericEditor` and `editorWindowOptions` are there to reuse. `Plugins/GainApp` is the example: one window with a toolbar of its own above the generic editor.

A plugin's MIDI output goes to the chosen port from a sender thread of its own, since sending is not safe on the audio thread. `Plugins/Synth` is `TestSynth` from `MakeASoundDSP` with its settings on parameters: a monophonic instrument that plays from a hardware port and the typing keyboard, shown through the generic editor. `Plugins/Gain` has a bypass parameter, which VST3 and AU hosts show as their bypass and AAX names as the plug-in's master bypass.

## VST3

The `VST3` format builds `<build>/VST3/<OUTPUT_NAME>.vst3`, a bundle any VST3 host scans: on macOS an ad-hoc signed bundle with identifier `<BUNDLE_ID>.vst3` (or `VST3_BUNDLE_ID`, see [bundle identifiers](#running-a-plugin-standalone)) that exports nothing but the three entry points, on Windows and Linux the standard `Contents/<arch>-win` and `Contents/<arch>-linux` layout. It is one single-component class per plugin over `PluginWrapper`, so the plugin code is the same code the standalone app runs:

- **Parameters** are listed by their host id, the 31-bit hash of the id path, so adding a parameter in a later release moves nobody's automation. Only automatable parameters are listed; values are read live and automation is applied before each block.
- **MIDI.** Notes, poly pressure and SysEx arrive as events. CC, channel aftertouch, pitch bend and program change arrive through hidden parameters the host maps them onto (`IMidiMapping`), and reach the plugin as ordinary `MIDI::Event`s at their sample offsets; a plugin's MIDI output goes back to the host the same way. Those hidden parameters, 16 channels × 131 controllers per MIDI input bus, exist only when `PluginDescription::vst3MidiControllers` says so; unset, an instrument has them and an effect or MIDI effect does not. Without them the mapping answers no controller, so the host's CC, bend, aftertouch and program change never reach the plugin; notes, poly pressure, SysEx and any controller a host sends as an event still do. AU, AUv3 and AAX deliver every MIDI message natively and ignore the setting. Their ids are MakeASound's (`0x7fff0000 | bus << 12 | channel << 8 | controller`, hidden and automatable) except under a JUCE `IdScheme`, where they are JUCE's (`0x6d636d00 + channel * 130 + controller`, one set whatever the bus, no program change, no flags), so a host's CC mapping in a JUCE session survives the port; `overrides.vst3Controllers` picks either layout, and the base of MakeASound's. JUCE has these whenever a plugin wants MIDI input, so a ported JUCE effect with a MIDI input sets `vst3MidiControllers = true`.
- **State** is the plugin's document, saved as a session (session-only parameters included) unless the host marks the save as a preset.
- **The editor** is the plugin's own `Editor`, or the generic page, embedded in the host's window. The host's own grip resizes it within the editor's constraints, and an editor's `requestResize` goes to the host as a resize of the plug-in window.
- **Bypass.** The plugin's bypass parameter (`{.bypass = true}`) is the host's bypass.

```bash
cmake -S . -B build -G Ninja   # -DMAKEASOUND_INSTALL_PLUGINS=OFF to skip the copy into the plug-in folder
cmake --build build
./build/Tools/PluginValidator/PluginValidator build/VST3   # validate every bundle there at strictness 10
```

`PluginValidator` is an ordinary executable target, so it runs from an IDE as well as from a shell. It takes the bundles to validate, or folders of them, as arguments and depends on nothing else in the build. It downloads Tracktion's [pluginval](https://github.com/Tracktion/pluginval) the first time (and never again for the same release), validates each bundle, leaves a log per bundle (`--logs <dir>`, the system temp folder by default) and exits with the number that failed. `--skip-gui-tests` leaves out the editor tests on a machine with no display; `--strictness`, `--timeout-ms` and `--version` set the rest. It validates `.component`s too (see [AU](#au)). CI runs it on macOS, Windows x64 and Linux. The test suite has a case that does the same through the library, run only when `MAKEASOUND_PLUGINVAL` is set (`nogui` skips the editor tests):

```bash
MAKEASOUND_PLUGINVAL=1 ./build/Tests/MakeASoundTests --test Pluginval/exampleBundlesPassAtStrictness10
```

## AU

On macOS the `AU` format builds `<build>/AU/<OUTPUT_NAME>.component`, an Audio Unit v2 bundle with identifier `<BUNDLE_ID>.component` (or `AU_BUNDLE_ID`), ad-hoc signed, exporting two symbols: `MakeASoundAUFactory`, which every component names, and `MakeASoundAUWritePlist`. Its `Info.plist` is written after every link by `MakeASoundAUPlistGen`, one tool shared by every plugin, which loads the module it was given and calls that second function with the module's own `describeModule()`, so the module description is the only place a plugin's identity lives: one `AudioComponents` entry per plugin, the module's `manufacturerCode` as the manufacturer and each plugin's `pluginCode` as the subtype. The type follows the category: an instrument is `aumu`, a MIDI effect `aumi`, an effect with a MIDI input `aumf` and any other effect `aufx`. It is one class over `PluginWrapper`, so the plugin code is again the same code the standalone app and the VST3 run:

- **Parameters** are listed by the same 31-bit host ids as in VST3, automatable ones only, ordered by `versionHint` and then host id as JUCE orders them, since Logic and GarageBand automate an AU parameter by its index in that list: give the first release's parameters `{.versionHint = 1}` and each later addition its release's higher number, and old automation stays put (a debug build asserts once when a parameter has none). Each comes with value strings for choices, the plugin's own text for every value, and its plain range, with a display flag where its curve is a square root, square, cube or cube root; a parameter with any other curve (a skewed `HzParam`, say) is published as 0..1, so the host's lane moves as the plugin's controls do. A port's profile changes this to its earlier build's list, ranges and flags (see [Plugins](#plugins) for `IdScheme`). A value the host writes reaches the plugin at the next block; one the editor writes is reported back to the host as a gesture.
- **MIDI.** Notes, CC (all-notes-off included), pitch bend, aftertouch, program change and short SysEx arrive at their sample offsets. A plugin with a MIDI output bus sends its events to the host through the MIDI output callback.
- **State** is the plugin's session document, carried in the host's preset dictionary beside the parameter values.
- **Reset** really resets: the host's `Reset` reaches the plugin's `reset()` at the next block.
- **Bypass.** A plugin with a bypass parameter answers `kAudioUnitProperty_BypassEffect`, so the host's bypass button and the parameter follow each other; one without refuses it.
- **The editor** is the plugin's own `Editor`, or the generic page, in the host's window. AU hosts give the window no grip, so the editor draws one; a resize request resizes the view and the host's container with it. When the window opens partly off screen, as Logic's can, it is moved onto it, or the editor is shrunk once where the window will not move; `launchctl setenv MAKEASOUND_AU_VIEW_TRACE 1` before starting the host logs what happened.

```bash
cmake -S . -B build -G Ninja   # each build also copies into ~/Library/Audio/Plug-Ins/Components
cmake --build build
./build/Tools/PluginValidator/PluginValidator build/AU          # auval -strict on every component there
./build/Tools/PluginValidator/PluginValidator build/VST3 build/AU   # the whole sweep
```

auval only finds a component that is installed, so `PluginValidator` copies each `.component` into `~/Library/Audio/Plug-Ins/Components` (replacing the one there) and runs `auval -strict -v` on each of its plugins, with `--stress N` adding auval's stress test. No pluginval download is needed for a run that names only components. If auval cannot find a freshly installed component, the tool restarts `AudioComponentRegistrar` once and tries again. CI runs it on macOS, and with `MAKEASOUND_PLUGINVAL` set the test suite's validation case sweeps the components too.

macOS caches a bundle's `AudioComponents` by its `CFBundleVersion`, which is `ModuleDescription::version`, not the CMake `VERSION` argument. Bump it whenever a plugin's codes, name, type or list change, or hosts keep seeing the old ones; during development `killall -9 AudioComponentRegistrar` clears the cache.

## AAX

AAX is opt-in. With `-DMAKEASOUND_BUILD_AAX=ON` on macOS, the `AAX` format builds `<build>/AAX/<OUTPUT_NAME>.aaxplugin`, an AAX Native bundle for Pro Tools with identifier `<BUNDLE_ID>.aaxplugin` (or `AAX_BUNDLE_ID`). It is ad-hoc signed and exports only the seven ACF entry points. On Windows the same folder holds one DLL, `Contents/x64/<OUTPUT_NAME>.aaxplugin`, which exports the same seven and has no plist or signature. Windows builds AAX for x64 only: Avid's SDK does not compile for Windows ARM64, and Pro Tools for Windows is x64, so an ARM64 tree skips the format with a status line. Pro Tools reads a plug-in's identity from the module, so the module description is again the only place it lives. Each plugin is one effect. Its manufacturer is `manufacturerCode` and its product is `pluginCode`. Its Pro Tools category is `PluginDescription::aaxCategory`, which defaults to Effect or Instrument by `category`. The effect has one component per mono or stereo stem pair the plugin accepts. It is one class over `PluginWrapper`, so the plugin code is again the same code every other format runs:

- **Parameters** are the automatable ones, by the same 31-bit host ids as in VST3 and AU (written as hex), with the plugin's own text for every value. A host write reaches the plugin at the next block. An editor edit is reported back to the host as a gesture, and a value the plugin moves on its own is shown to the host on its next timer.
- **MIDI.** Notes, CC, pitch bend, aftertouch, program change and short SysEx arrive at their sample offsets. A plugin with a MIDI output bus sends channel messages back to Pro Tools.
- **State** is the plugin's session document, in one chunk of the session. Pro Tools' compare light compares the parameters, leaving out bypass.
- **Bypass.** A plugin with a bypass parameter names it as its master bypass, and the plugin does the bypassing itself, as Gain does.
- **The editor** is the plugin's own `Editor` or the generic page. A Pro Tools plug-in window has no grip, so the editor draws one. A resize request goes to Pro Tools, which sizes the window. On Windows the editor sits in the `HWND` Pro Tools gives it; how it scales on a HiDPI display there is not yet checked.
- **Blocks** longer than 1024 samples are rendered in slices of 1024, each with its own MIDI and playhead.

```bash
cmake -S . -B build -G Ninja -DMAKEASOUND_BUILD_AAX=ON
cmake --build build
./build/Tools/PluginValidator/PluginValidator --aax-tools ~/AAX/Validator build/AAX   # aaxval on every .aaxplugin there
```

**Licensing.** Avid's AAX SDK is dual-licensed: you take Avid's AAX SDK License Agreement, or GPLv3. MakeASound neither vendors nor copies any of it. The configure fetches it, from a public fork of SDK 2.8.1 pinned to one commit, only when `MAKEASOUND_BUILD_AAX` is on (`-DCPM_AAXSDK_SOURCE=<path>` uses your own copy). So the repository stays MIT, and a build with the flag off never sees Avid's code. An AAX binary you build with it is yours to license: it is GPLv3 unless you hold Avid's agreement, which shipping AAX needs anyway. The configure says so once.

**Validation and signing.** Avid's AAX Validator and its DigiShell are NDA-confidential developer tools. MakeASound never fetches, ships or runs them in CI. `PluginValidator` runs your own copy from the folder that `--aax-tools` or the `MAKEASOUND_AAX_TOOLS` environment variable names (the one holding `CommandLineTools/dsh`), test by test. Two tests are skipped, each logged with its reason: the HDX-only `test.cycle_counts`, and `test.page_table.load`, which needs a page table these plug-ins do not ship. Only one validator can run at a time. Unpacking the validator may leave it quarantined; `xattr -dr com.apple.quarantine <folder>` once fixes that, and the tool says so when it is the cause. Pro Tools Developer loads the bundle as built, ad-hoc signed on macOS and unsigned on Windows. Retail Pro Tools loads only bundles signed with PACE's tools, under Avid's agreement. If you have them, pass your wrap configuration to `makeasound_add_plugin` as `AAX_WCGUID <guid>` and configure with `-DMAKEASOUND_PACE_SIGN_ID="Developer ID Application: …"` (and optionally `MAKEASOUND_PACE_ACCOUNT`); each build then runs `wraptool sign` on the bundle. No password goes in the build: run `wraptool sync --account <account> --password <password>` once, and wraptool keeps it in your keychain. A failed signing is a warning and leaves the ad-hoc signature. Validation through `PluginValidator` and PACE signing are macOS-only for now; on Windows, `AAX_WCGUID` only prints a status line saying signing is not wired there yet. CI builds both bundles and runs the AAX test cases on macOS and on both Windows x64 jobs, but neither validates nor signs.

## AUv3 on iOS

On iOS the `AUv3` format builds an app extension inside the standalone app, `<OUTPUT_NAME>.app/PlugIns/<OUTPUT_NAME>.appex` with identifier `<BUNDLE_ID>.auv3` (or `AUV3_BUNDLE_ID`, which must begin with `<BUNDLE_ID>.`), so it needs `Standalone` in the same call (your own app from `STANDALONE_SOURCES` works too). Installing the app registers the plugin with every host on the device. The extension is an `AUAudioUnit` over the same `PluginWrapper`, running out of the host's process: parameters as an `AUParameterTree` grouped like your `ParameterGroup`s, MIDI and automation at their sample offsets, both a preset and a session state, the host's bypass, latency and tail, and the plugin's editor (or the generic one) in the host's window, sized by the editor's constraints. macOS keeps the `.component` and gets no AUv3.

The extension's `Info.plist` lists the components from `describeModule()`, but an iOS binary cannot run on the Mac that builds it, so an iOS tree builds a small tool from your plugin's sources a second time for macOS, in a sub-build of the same project that reuses every source CPM already fetched, and runs it. Your plugin's sources must therefore compile for macOS as well; `MAKEASOUND_DESCRIBE_CMAKE_ARGS` passes that sub-build any configure arguments your tree needs.

```bash
# Ninja: the simulator, signed ad hoc (a device build comes out unsigned)
cmake -S . -B build-ios-sim -G Ninja -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build-ios-sim
xcrun simctl install booted "build-ios-sim/Plugins/Gain/MakeASound Gain.app"
xcrun simctl spawn booted "$PWD/build-ios-sim/Tools/AUv3Probe/AUv3Probe" \
      "$PWD/build-ios-sim/Plugins/Gain/MakeASound Gain.app"

# Xcode: signed for a device with your team, or ad hoc for the simulator without one
cmake -S . -B build-ios-xcode -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DMAKEASOUND_IOS_TEAM=<team id>
```

`AUv3Probe` does what a host does first: it finds each installed component, instantiates it out of process, renders a sine through an effect or a note through an instrument, and opens the view; CI runs it on both examples in the simulator.


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
  MakeASoundCore.h  the core's umbrella: Audio/, Common/, Realtime/ and MIDI's
                    events and parser, no device header
  Audio/            Buffer, Channel, and the Processor / ProcessContext / BusLayout vocabulary
  Devices/          DeviceInfo data types, DeviceManager façade, Engine, device queries
  MIDI/             typed events, port info, block sync, MidiManager façade
  Realtime/         SPSCQueue, SpinLock, ScopedNoDenormals, Smoother
  Plugin/           MakeASoundPlugin.h, the plugin core's umbrella header
    Parameters/     Parameter, FloatParam, ChoiceParam, BoolParam, ParameterGroup, ParameterList
    State/          StateContext, State
    Core/           Description, Editor (size policy and EditorHost), HostEditListener, Plugin, StatePlugin
    Host/           PluginWrapper, the per-block pipeline every format adapter shares
    Realtime/       MessageThread, RealtimeSwap
    UI/             MakeASoundPluginUI: the generic parameter editor and the resize grip
    Standalone/     MakeASoundStandaloneCore (StandaloneHost, no window) and MakeASoundStandalone,
                    the default app: desktop windows and menu bar, or one iOS screen
    VST3/           MakeASoundVST3: the VST3 format, its bundle plist, PkgInfo and entry point
    AU/             MakeASoundAU: the AU format, its factory, Cocoa view, view placement and plist tool
    AAX/            MakeASoundAAX (opt-in): the AAX format, its description, view, plist and entry point
    Validation/     MakeASoundPluginval: fetches and runs pluginval, runs auval and your own aaxval
  DSP/              MakeASoundDSP, optional: TestSynth, the instrument the Synth app and plugin share
  UI/               dropdown/toggle-list helpers for the demo apps
  Common/           EA type re-exports and audio-thread-safe algorithms
  MiniAudio/        audio backend (hidden)
  CoreMIDI/         MIDI backend, Apple (hidden)
  WinMIDI/          MIDI backend, Windows WinMM (hidden)
  ALSA/             MIDI backend, Linux sequencer (hidden)
Apps/               AudioProbe (GPU/UI, iOS too), Example, MidiDemo (CLI),
                    Demo, Synth (web UI)
Plugins/            Gain, Synth: plugins built with makeasound_add_plugin;
                    GainApp: Gain in a standalone app of its own
Tools/              PluginValidator: pluginval, auval or aaxval over the bundles on its command line
CMake/              CPM, the Find modules, MakeASoundPlugin.cmake,
                    ExternalFolders.cmake, the plug-in install script
ThirdParty/         the vendored VST3 SDK, and the builds of the fetched AudioUnitSDK and (opt-in) AAX SDK
Tests/              NanoTest suites
gaps.md             what the probe found, what was fixed, what is open
```

Data structs opt into JSON reflection in place via `MIRO_REFLECT(...)`, so a `StreamConfig` round-trips through [Miro](https://github.com/eyalamirmusic/Miro) without any extra code:

```cpp
Miro::logJSON(manager.getDefaultConfig());
```

## License

MIT; see [LICENSE](LICENSE). Every dependency is MIT or more permissive but one,
and the VST3 SDK vendored under `ThirdParty/VST3_SDK` is MIT as of 3.8.0, its
notices kept alongside. The one is Apple's AudioUnitSDK, fetched on macOS for the
AU format, which is Apache-2.0. A plugin built on MakeASound ships those notices
with it. Avid's AAX SDK is not a dependency of a default build. It is fetched only
with `MAKEASOUND_BUILD_AAX`, and it is GPLv3 unless you hold Avid's agreement, so
an AAX binary built with it is too (see [AAX](#aax)).

## Dependencies

Fetched automatically: [miniaudio](https://github.com/mackron/miniaudio), [Miro](https://github.com/eyalamirmusic/Miro), `ea_data_structures`, plus [eacp](https://github.com/eyalamirmusic/eacp) for the apps and the plugin core, Apple's [AudioUnitSDK](https://github.com/apple/AudioUnitSDK) (tag `AudioUnitSDK-1.4.0`) for the AU format on macOS, Avid's AAX SDK 2.8.1 (from the [audiosdk/aax](https://github.com/audiosdk/aax) fork, pinned to a commit) for the AAX format, only with `MAKEASOUND_BUILD_AAX`, and [NanoTest](https://github.com/eyalamirmusic/NanoTest) + [ScopedMemoryAllocations](https://github.com/eyalamirmusic/ScopedMemoryAllocations) for the tests. No MIDI library is fetched: Core MIDI, WinMM and the ALSA sequencer come with the platform. Miro is linked `PUBLIC` by `MakeASoundCore` (it leaks through the reflected data structs), and is all the core links; miniaudio and whichever MIDI library the platform selected are `PRIVATE` to `MakeASound`, fully hidden behind the façades, and `MakeASound` links the core `PUBLIC`. `MakeASoundPlugin` and `MakeASoundDSP` link `MakeASoundCore` `PUBLIC`, not the device library; only `MakeASoundStandaloneCore` adds `MakeASound`. `MakeASoundPlugin` links `eacp-core` `PRIVATE`, and none of its headers includes eacp; `MakeASoundPluginUI` adds `eacp-ui` and `MakeASoundStandalone` `eacp-graphics`, both `PUBLIC`. The React demo apps pull React and vite through npm.
