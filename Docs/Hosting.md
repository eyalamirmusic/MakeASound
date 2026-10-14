# Hosting third-party plugins

`MakeASoundHosting` (`Lib/MakeASound/Hosting/`, namespace `MakeASound::Hosting`,
umbrella `<MakeASound/Hosting/MakeASoundHosting.h>`) is the other side of the
plugin framework: a host's view of plugins somebody else built. It sits beside
`Plugin/` rather than in it, because `Plugin/Host/` is the *plugin's* view of a
host.

It **finds and describes** plugins (VST3 everywhere it builds, Audio Units on
macOS, scanned out of process into a database) and **loads and runs** VST3
plugins in process. Audio Unit loading and hosted editors come next (see
[What is not here yet](#what-is-not-here-yet)). Scanning has its own document,
[PluginScanning.md](PluginScanning.md).

## Loading and running a plugin

```cpp
auto formats = MakeASound::Hosting::FormatManager();
formats.addDefaultFormats();

auto loaded = formats.load(description, {.name = "My Host"});   // message thread

if (loaded.plugin == nullptr)
    showError(loaded.error);

engine.start(config, *loaded.plugin);   // a HostedPlugin is a Processor
```

`description` is a row of the plugin database (`PluginCache::plugins()`), so a
plugin is addressed by its path and class id, never by a position, and a saved
selection survives a plugin update. `HostInfo::name` is what the plugin is told
the host is called.

A `HostedPlugin` is a `MakeASound::Plugin`: `getBusLayout()` is the plugin's own
buses (with `activeByDefault` as it reported them, so a sidechain it ships off
stays off), `acceptsLayout` asks it, `prepare` and `process` run it, and
`saveState`/`loadState` are its own opaque chunk. It is never put in a
`PluginWrapper`, which adapts our plugins to a host, and it declines the added
bypass (`wantsHostBypass()`).

Its `parameters()` are the plugin's, in its order, hidden ones left out. Each has
the plugin's own id as its host id, any of the 32 bits (`hostedPluginIdentity()`),
that id in decimal as its id, and the plugin's normalized range as its plain one
(0..1, stepped where the plugin says). Text goes through the plugin
(`valueToText`, `textToValue`). Setting a value from the message thread
(`setValue`) reaches the plugin's controller and its processor's next block;
`setParameterFromAudioThread` does the same from the audio thread, for a host's
automation.

What only a host needs is on `HostedPlugin`:

| call | thread | what |
| --- | --- | --- |
| `setParameterFromAudioThread(i, v)` | audio | a value for the next block, allocation-free |
| `syncEditorWithProcessor()` | message (UI timer) | values the processor reported, pushed to the plugin's own GUI |
| `refreshParameterValue(i)` | message | re-read a value the plugin's GUI moved without saying (AU) |
| `takeParameterInfoChanged()` | any | the plugin renamed parameters, already re-read in place |
| `takeLatencyChanged()` | any | `latencySamples()` moved |
| `takeIoChanged()` | any | the buses changed: renegotiate and prepare again |
| `setFallbackPlayhead(p)` | message, not processing | the transport for a context with none; stopped at 120 BPM by default |

The plugin's own GUI reaches the host through the `HostEditListener` slot every
`Plugin` has: begin, perform and end of each edit by parameter index, plus
`parameterInfoChanged` and `latencyChanged`, on the message thread. That is how a
host learns which control the user touched.

`VST3/VST3HostedPlugin.h` is the VST3-only side, for code that must reach the
plugin itself: its `IEditController`, the `IComponentHandler` this host installed
(what the plugin's GUI calls), and whether its controller is a class of its own.

A VST3 load enters the bundle's folder for the span of the load, as JUCE's host
does, connects a separate controller to its component and hands it the component's
state before anything asks for a view (some plugins read that state in
`attached()`).

## Building it

`MAKEASOUND_BUILD_HOSTING` (on for a top-level build, off as a dependency) needs
`MAKEASOUND_BUILD_PLUGIN`. The target builds on macOS and Windows for now; Linux
and iOS come later, and the per-platform files below are where they slot in.

There are to be two libraries:

| target | holds | links |
| --- | --- | --- |
| `MakeASoundHosting` | formats, scanning, the cache, loading and processing | `MakeASoundPlugin`, `vst3sdk-hosting`, eacp-core, AudioToolbox on macOS |
| `MakeASoundHostedEditor` | (next) a hosted plugin's own editor in an eacp view | the above, eacp-graphics |

The split keeps a scanner's worker process, which loads a plugin only to describe
it, free of the graphics stack.

### `makeasound_enable_hosting(<target>)`

Turns a target that already exists, an app or a plugin, into a host:

```cmake
add_executable(MyHost main.cpp)
makeasound_enable_hosting(MyHost)
```

It links `MakeASoundHosting` and, on macOS, seals an executable's exports
(`-no_exported_symbols`). The reason is C++ weak definitions: template
instantiations, inline functions, vtables and typeinfo are emitted into every
image that uses them, and dyld coalesces them across the process onto the first
copy it saw, which is the host's. A plugin built against other revisions of a
library the host also links (EA data structures, Miro, eacp, this one) would then
run the host's copy of an inline function on its own differently laid-out data.
The mangled name encodes a type's name, never its layout, so nothing catches it.
An executable that exports nothing keeps its definitions to itself. A plugin bundle
built with `makeasound_add_plugin` is sealed by its own export list already, so a
plugin that hosts (an instrument that loads other plugins) gets only the link.

No stand-in for the VST3 SDK's `moduleHandle` is needed: `moduleinit.cpp`, which
references it, is an object of its own in `vst3sdk` that no host pulls in.

## Formats

A `HostFormat` is one format's host side: where it installs, how a target is
recognised from its spelling, whether one is installed, its stamp, and a scan.
`FormatManager` owns one per format and is passed to whatever needs them, so there
is no process-wide registry and no static initializer to keep alive:

```cpp
auto formats = MakeASound::Hosting::FormatManager();
formats.addDefaultFormats();   // VST3, then AU on macOS
```

`addDefaultFormats()` is defined per platform (`DefaultFormats-<os>.cpp`). A host
that wants a subset, or a format of its own, calls `add()` instead. Formats claim
targets in the order they were added: a `*.vst3` path is VST3's, a
`type:subtype:manufacturer` identifier is AU's.

| | VST3 scan | AU scan | VST3 load | AU load | hosted editors |
| --- | --- | --- | --- | --- | --- |
| macOS | yes | yes (v2, and v3 through the v2 API) | yes | next | next |
| Windows | yes | - | yes | - | next (HWND child, DPI conversion) |
| Linux | later | - | later (headless) | - | later |
| iOS | later | later | later | later | later |

CLAP hosting and out-of-process audio processing are not planned.

## Threading

The **message thread** has the format manager, the cache, the sweep and its pool,
loading, preparing, layouts, state, and every callback any of them fires, the
`HostEditListener` ones included. Nothing creates a thread of its own except
`ScanMode::InProcess`, which scans on a background thread for tests and debugging.
A sweep advances in `poll()`, which a console program calls in a loop and a GUI
hands to an eacp timer with `setAutoPoll(true)`.

The **audio thread** has only `process` and `setParameterFromAudioThread`, both
allocation-free (`Tests/HostingAllocationTests.cpp`): every buffer, table and
queue is sized in `prepare`, and a block longer than prepared is refused. Edits
from the message thread reach the processor through an SPSC queue drained at the
top of each block; values the processor reports reach the message thread through
atomics and a dirty flag that `syncEditorWithProcessor()` consumes. A bus the
caller does not supply reads silence and writes into a scratch channel, since a
VST3 plugin may touch every channel of every bus it declares.

## Platform code

Selected in CMake, never with `#if`, like the rest of MakeASound:

- `DefaultFormats-<os>.cpp`, `Scan/ExecutablePath-<os>.cpp`,
  `Scan/WorkerProcess-<os>.cpp`, `VST3/VST3Folders-<os>.cpp`
- `AU/`, added on macOS only
- the SDK's `module_mac.mm` / `module_win32.cpp` / `module_linux.cpp`, one per
  platform in `vst3sdk-hosting` (`ThirdParty/CMakeLists.txt`)

Every VST3 TU includes `Plugin/VST3/VST3Common.h` first, as the format adapter's
do.

## What is not here yet

The rest of Tamber Plug's hosting layer moves here in steps. Waiting:

- **Audio Unit loading**: a `HostedPlugin` over the AudioUnit C API (v2, and v3
  through it, out of process first), with `refreshParameterValue`.
- **Hosted editors** (`MakeASoundHostedEditor`) for VST3 on macOS and Windows and
  for AU on macOS.
- **`Apps/PluginHost`**, the GUI example: the list, the filter, scan progress, a
  plugin in `Engine`, its editor and a typing keyboard.
