# MakeASound

A C++20 library for platform-agnostic audio, MIDI and audio plugins.

## Features

**Audio devices** (`DeviceManager`, backed by [miniaudio](https://github.com/mackron/miniaudio))

- Every audio API the machine offers, selectable at runtime: Core Audio, WASAPI, DirectSound, WinMM, ALSA, PulseAudio, JACK, AAudio, OpenSL.
- Device enumeration, channel selection, sample rate and block size, latency queries.
- Automatic recovery when the OS stops a device (unplug, rate change), with a `dirty` flag on the callback.
- iOS audio session handling that follows the stream, with overrides.
- Errors are returned as values, never thrown.

**MIDI** (`MidiManager`)

- Core MIDI on macOS and iOS, WinMM on Windows, the ALSA sequencer on Linux.
- Input and output ports, plus virtual ports on Core MIDI and ALSA.
- Typed events (note, CC, pitch bend, aftertouch, program change, SysEx) with allocation-free conversion to and from bytes.
- Callback delivery, or a queue drained in the audio callback with sample-accurate offsets.

**Processing** (`MakeASoundCore`)

- A planar `Buffer` that owns or refers to memory, sliced by samples or channels without allocating.
- A `Processor` interface with buses and MIDI, run on a device stream by `Engine`.
- Real-time safe helpers: `SPSCQueue`, `Smoother`, `ScopedNoDenormals` and more.
- The core links no device backend, so DSP and plugin code can use it alone.

**Plugins** (`MakeASoundPlugin`): write a plugin once and build it as:

| Format | Platforms |
| --- | --- |
| Standalone app | macOS, Windows, iOS |
| VST3 | macOS, Windows, Linux |
| AU v2 | macOS |
| AUv3 | iOS |
| AAX (opt-in, `-DMAKEASOUND_BUILD_AAX=ON`) | macOS, Windows x64 |

- Parameters declared in nested groups, with stable host ids and automation in every format.
- Versioned JSON state for presets and sessions.
- Sample-accurate MIDI in and out, host bypass.
- Custom editors with a size policy (min, max, aspect ratio), or a generic parameter editor.
- A standalone app with device and MIDI settings, a computer keyboard, and settings kept across launches, or a windowless host for an app of your own.
- Compatibility profiles that keep the parameter ids and state of plugins ported from JUCE or Tamber Plug.
- Example plugins pass pluginval at strictness 10, `auval -strict` and Avid's AAX Validator.

**Hosting** (`MakeASoundHosting`, macOS and Windows)

- Scans VST3 plugins and Audio Units out of process into a plugin database.
- Loads and runs them, with their editors. See [Docs/Hosting.md](Docs/Hosting.md) and [Docs/PluginScanning.md](Docs/PluginScanning.md).

**Tools**

- `PluginValidator` runs pluginval, auval or your own copy of Avid's validator over built bundles.
- `AUv3Probe` loads AUv3 extensions in the iOS simulator.

## Requirements

- CMake 3.31+ and a C++20 compiler.
- macOS 11+, iOS 15+, Windows (x64 and ARM64) or Linux.
- Linux needs `libasound2-dev`, plus the graphics and libcurl dev packages listed in `.github/workflows/ci.yml`.

Dependencies are fetched by [CPM.cmake](CMake/CPM.cmake) at configure time.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

./build/Apps/Example/Example      # streams white noise for 2 seconds
./build/Apps/MidiDemo/MidiDemo    # opens virtual MIDI ports and sends notes
./build/Tools/PluginValidator/PluginValidator build/VST3 build/AU
```

Plug-in builds are copied into the user's plug-in folder; pass `-DMAKEASOUND_INSTALL_PLUGINS=OFF` to skip that.

For iOS:

```bash
cmake -S . -B build-ios -G Ninja -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build-ios
```

Main options: `MAKEASOUND_BUILD_DEVICES`, `MAKEASOUND_BUILD_PLUGIN`, `MAKEASOUND_BUILD_HOSTING`, `MAKEASOUND_BUILD_AAX`, `MAKEASOUND_INSTALL_PLUGINS`, `MAKEASOUND_IOS_TEAM`.

## Using it in your project

```cmake
include(FetchContent)

FetchContent_Declare(MakeASound
        GIT_REPOSITORY https://github.com/eyalamirmusic/MakeASound.git
        GIT_TAG main)
FetchContent_MakeAvailable(MakeASound)

target_link_libraries(MyApp PRIVATE MakeASound)
```

Set options before `FetchContent_MakeAvailable`. For the core alone, with no device backend:

```cmake
set(MAKEASOUND_BUILD_DEVICES OFF)
FetchContent_MakeAvailable(MakeASound)

target_link_libraries(MyDSP PRIVATE MakeASound::Core)   # <MakeASound/MakeASoundCore.h>
```

For plugins, set `MAKEASOUND_BUILD_PLUGIN` on and link `MakeASoundPlugin`.

## Audio

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

Buffers slice without allocating:

```cpp
auto output = info.getOutput();
output.getSubBuffer(0, event.sampleOffset).clear();
output.getChannelSubset(2, 2).applyGain(0.5f);
```

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

### Reacting to the device

```cpp
for (auto notification: manager.drainNotifications())  // from a UI timer
    updateUI(notification);

manager.setAutoRecover(false); // own the "device lost" decision yourself
```

### The audio session (iOS)

```cpp
auto session = MS::SessionConfig {};
session.options.mixWithOthers = true;
session.category = MS::SessionCategory::Ambient;  // unset follows the stream

manager.setSessionConfig(session);
manager.start(manager.getDefaultOutputConfig(), callback);
```

## MIDI

Callback mode fires on the platform's MIDI thread:

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

Queue mode lets an audio callback drain events with sample offsets resolved:

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

## Processors

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

## Plugins

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

One CMake call builds every format; a format the platform can't build is skipped:

```cmake
makeasound_add_plugin(Gain
        FORMATS Standalone VST3 AU AUv3 AAX
        OUTPUT_NAME "MakeASound Gain"
        SOURCES GainPlugin.cpp)
```

`Plugins/Gain` and `Plugins/Synth` are complete examples.

### Editors

`createEditor()` returns a custom editor, or nothing for the generic one:

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

### A standalone app of your own

Replace the default standalone app with your own sources, built on the windowless `StandaloneHost` (see `Plugins/GainApp`):

```cmake
makeasound_add_plugin(GainApp
        FORMATS Standalone
        OUTPUT_NAME "MakeASound Gain App"
        SOURCES ../Gain/GainPlugin.cpp
        STANDALONE_SOURCES GainApp.cpp)
```

## License

MIT; see [LICENSE](LICENSE). The vendored VST3 SDK is MIT, and Apple's AudioUnitSDK (fetched for AU on macOS) is Apache-2.0. Avid's AAX SDK is fetched only with `MAKEASOUND_BUILD_AAX` and is GPLv3 unless you hold Avid's license agreement, so an AAX binary built with it is too.
