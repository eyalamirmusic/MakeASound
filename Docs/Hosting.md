# Hosting third-party plugins

`MakeASoundHosting` (`Lib/MakeASound/Hosting/`, namespace `MakeASound::Hosting`,
umbrella `<MakeASound/Hosting/MakeASoundHosting.h>`) is the other side of the
plugin framework: a host's view of plugins somebody else built. It sits beside
`Plugin/` rather than in it, because `Plugin/Host/` is the *plugin's* view of a
host.

What it does today is **find and describe** plugins: VST3 everywhere it builds,
Audio Units on macOS, scanned out of process into a database. Loading a plugin
and showing its editor come next (see [What is not here yet](#what-is-not-here-yet)).
Scanning has its own document, [PluginScanning.md](PluginScanning.md).

## Building it

`MAKEASOUND_BUILD_HOSTING` (on for a top-level build, off as a dependency) needs
`MAKEASOUND_BUILD_PLUGIN`. The target builds on macOS and Windows for now; Linux
and iOS come later, and the per-platform files below are where they slot in.

There are to be two libraries:

| target | holds | links |
| --- | --- | --- |
| `MakeASoundHosting` | formats, scanning, the cache, and (next) loading and processing | `MakeASoundPlugin`, `vst3sdk-hosting`, eacp-core, AudioToolbox on macOS |
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
| macOS | yes | yes (v2, and v3 through the v2 API) | next | next | next |
| Windows | yes | - | next | - | next (HWND child, DPI conversion) |
| Linux | later | - | later (headless) | - | later |
| iOS | later | later | later | later | later |

CLAP hosting and out-of-process audio processing are not planned.

## Threading

Everything in the module today runs on the **message thread**: the format
manager, the cache, the sweep and its pool, and every callback they fire. Nothing
creates a thread of its own except `ScanMode::InProcess`, which scans on a
background thread for tests and debugging. A sweep advances in `poll()`, which a
console program calls in a loop and a GUI hands to an eacp timer with
`setAutoPoll(true)`.

Loading will add the audio thread, and the rules the design keeps for it: only
`process` and setting a parameter from the audio thread run there, both
allocation-free; edits from the UI reach the processor through an SPSC queue
drained at block start; values from the processor reach the UI through atomics and
a dirty flag consumed on a UI timer.

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

- **`HostedPlugin`**, a `MakeASound::Plugin` wrapping a loaded VST3 or AU, with
  `setParameterFromAudioThread`, `syncEditorWithProcessor`,
  `refreshParameterValue`, `takeParameterInfoChanged`, `takeIoChanged` and a
  fallback playhead (stopped by default). It needs the plugin core to carry
  parameters it did not derive: foreign host ids, renames in place
  (`Parameter::setDisplayInfo`), a bypass opt-out and default-inactive buses.
  Those wait on the parameter-identity redesign.
- **`HostFormat::load` / `FormatManager::load`**, which return a `HostedPlugin`.
- **Hosted editors** (`MakeASoundHostedEditor`) for VST3 on macOS and Windows and
  for AU on macOS.
- **`Apps/PluginHost`**, the GUI example: the list, the filter, scan progress, a
  plugin in `Engine`, its editor and a typing keyboard.
