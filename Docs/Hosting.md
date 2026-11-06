# Hosting third-party plugins

`MakeASoundHosting` (`Lib/MakeASound/Hosting/`, namespace `MakeASound::Hosting`,
umbrella `<MakeASound/Hosting/MakeASoundHosting.h>`) is the other side of the
plugin framework: a host's view of plugins somebody else built. It sits beside
`Plugin/` rather than in it, because `Plugin/Host/` is the *plugin's* view of a
host.

It **finds and describes** plugins (VST3 everywhere it builds, Audio Units on
macOS, scanned out of process into a database), **loads and runs** them in
process, and **shows their own editors** (see [Editors](#editors)). What is still
to come is under [What is not here yet](#what-is-not-here-yet). Scanning has its
own document, [PluginScanning.md](PluginScanning.md).

## The example: `Apps/PluginHost`

[`Apps/PluginHost`](../Apps/PluginHost) is the reference for everything below, in
about five hundred lines. [`PluginHost.h`](../Apps/PluginHost/PluginHost.h) is
the hosting itself, with no UI:

- a `FormatManager` with the default formats;
- a `PluginCache` at this app's own default location
  (`defaultPluginCachePath()`), stamped through the formats;
- a `ScanSession` whose workers are the app relaunched;
- the database listed and filtered (`filterPlugins`);
- a load through `FormatManager::load`, with `hostedEditors()` as the editors;
- the plugin run by `Engine` on the devices and MIDI inputs the user picked,
  kept as the standalone keeps its own (`Standalone::Settings`, re-pointed at
  today's devices by name through `resolveConfig` and `resolvePortIds`); with
  nothing picked, the default output, duplex only when the plugin has an input,
  and every MIDI input;
- `syncEditorWithProcessor()` from a UI timer.

[`Main.cpp`](../Apps/PluginHost/Main.cpp) answers `--makeasound-scan` (the example
protocol, `Apps/ScanProtocol`) before anything else and points its sweeps at
itself relaunched, then puts one window on it (filter, Scan, Rescan All, the list,
Show Editor, Audio / MIDI, a status line), the plugin's editor in a second window
that resizes when the plugin asks, and the standalone's `SettingsPanel` in a
third, picking the output and input devices and channels, the sample rate, the
block size and the MIDI inputs. The tests drive the `Host` class headless over the
fixture plugins, and run the app binary as its own scan worker.

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
| `saveControllerState()` / `loadControllerState(s)` | message | a VST3 controller's own `getState`/`setState`, which `saveState` leaves out; load it after `loadState`; empty for an AU |
| `indexForControllerIndex(i)` | any | the `parameters()` index of the parameter a JUCE host listed at `i`, or -1 |

**Sessions a JUCE host saved.** JUCE's VST3 host stored a hosted plugin as
`copyXmlToBinary` of `<VST3PluginState>` with the component's and the
controller's state in JUCE base64. `Compat::juceVST3HostState` reads that blob
and `Compat::toJuceVST3HostState` writes it byte for byte (`Plugin/Compat/
JuceState.h`); restore it with `loadState(component)` then
`loadControllerState(controller)`, JUCE's order. An AU's JUCE state is the same
ClassInfo `saveState` writes. `Compat::juceHostedUniqueId` and
`juceHostedDeprecatedUid` (`JuceHostCompat.h`, no SDK header) compute the ids
JUCE's host gave a plugin from its cache row, so a saved description resolves
without a scan. JUCE listed every parameter, a VST3's hidden ones and an AU's
unaddressable ones included, its parameter tree flattened (a unit's or a clump's
parameters gathered where the group first appears,
`Compat::juceParameterTreeOrder`); `indexForControllerIndex` maps such an index
here. For a plugin whose groups are contiguous and that hides nothing ahead of a
visible parameter it is the identity.

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

MIDI reaches a VST3 as note, poly-pressure and SysEx events (`kDataEvent`,
`kMidiSysEx`, F0 to F7, pointing at the block's own event), and controllers, bend,
channel pressure and program change through `IMidiMapping`; what the plugin sends
comes back into `midiOut`, SysEx up to `MIDI::SysEx::maxBytes` (a longer one is
dropped, as the Audio Unit host drops it).

`prepare()` hands the loaded plugin the `ProcessSpec`'s rate as it is, a `double`
that need not be whole (VST3's `ProcessSetup::sampleRate` and `ProcessContext`, an
Audio Unit's stream formats), so a host passes its own host's rate on unrounded.

`AU/HostedAudioUnit.h` is the Audio Unit side (macOS): the `AudioUnit` itself and
`loadAudioUnit(component, description, host)`, which takes a component already
found or registered (the tests register their own and never search the
registry). A v2 component is instantiated directly; a v3 extension out of process
first, as Logic loads it, and in process only if that fails, each with a timeout.
Parameters are Global-scope, in the unit's own plain range, with an indexed one's
value strings as its text; the unit's own view reaches the listener through an
AUEventListener on the main run loop, and `refreshParameterValue(i)` re-reads a
value an AU editor moved without notifying. A unit's property listeners may run
on any thread, so a rename it announces (`ParameterInfo`, or `ParameterList` with
the same ids) is read back on the main run loop: `takeParameterInfoChanged()` and
the listener's `parameterInfoChanged` see it once that loop turns. A latency
change raises `takeLatencyChanged()` at once and reaches the listener's
`latencyChanged` on the main run loop. `latencySamples()` and `tailSamples()`
convert the unit's figures in seconds at the rate last prepared, so a latency the
unit announces while `prepare()` runs lands in the new rate. Destroying the unit
waits for a property listener still running in it, and one the unit calls after
that reaches nothing. A list whose ids changed, grown or shrunk,
leaves `parameters()` as it was and raises `takeIoChanged()`: reload the plugin.
The list a unit gives once `prepare()` initialized it is the set, and a unit that
lists nothing until initialized (loading initializes it once to read the list)
keeps its set while uninitialized: neither is a change.
`syncEditorWithProcessor()` tells the unit's other listeners (its view) about
values set from the audio thread, never the host's own. The state is
`kAudioUnitProperty_ClassInfo` as a binary property list. Values from the audio
thread go out as `AudioUnitScheduleParameters` events at the next render. A unit
with MIDI out lists its outputs (`kAudioUnitProperty_MIDIOutputCallbackInfo`) as
`midiOutputs`, and what it sends through `kAudioUnitProperty_MIDIOutputCallback`
during a render lands in the block's `midiOut`, each packet's time stamp its
sample offset. An AUv3 reaches the host the same way through the v2 bridge, so
`MIDIOutputEventListBlock` is not asked for.

## Editors

The hosting library draws nothing. A host that shows a plugin's own editor links
`MakeASoundHostedEditor` and hands its factory to the load:

```cpp
auto loaded = formats.load(description, {.name = "My Host",
                                         .editors = MakeASound::Hosting::hostedEditors()});
auto editor = loaded.plugin->createEditor();   // null when the plugin has none
```

No static registrar and no linker hard reference: the host passes the factory, and
a host that passes none gets no editors. `createEditor()` answers a
`HostedEditor`, a MakeASound `Editor` any window embeds (the standalone's
`EditorWindow`, a plugin's own view through `EmbeddedView`), plus
`constrainSize()`, the nearest size the plugin accepts.

- **VST3** (`HostedVST3Editor`, macOS and Windows): the `IPlugView` parented into an
  eacp `NativeChildSurface` (an `NSView`, or a child `HWND`) from `onAttached()`,
  the earliest a child window exists. Sizes are points to the host; on Windows the
  plugin's `ViewRect`s are pixels, converted against the child window's DPI, and
  the plugin is told the scale through `setContentScaleFactor`, the only way it
  learns it there. On macOS it is told 1 before attaching. A plugin's
  `resizeView` becomes `Editor::requestOwnResize`.
- **Audio Units** (`HostedAudioUnitEditor`, macOS): the unit's `CocoaUI` view (an
  AUv3 answers it too through the v2 API), else Apple's `AUGenericView`, which is
  the only resizable kind; a view that changes its own frame asks the host to
  follow, through `Editor::requestOwnResize`.

`constraints().resizable` is whether the host's user may drag the plugin's window
(`canResize()` on VST3; only the generic view on AU). A resize the plugin asks for
itself, from its own zoom menu or a tab change, is honoured either way, as Tamber
Plug's host did: the hosted editor hands it to its `EditorHost::resizeEditor`
whether or not the view is resizable, and the plugin hears that it took (VST3
`kResultOk`).

The platform parts are per-platform TUs (`HostedEditors-<os>.cpp`,
`VST3/VST3EditorPlatform-<os>.cpp`, `AU/` on macOS). The editor library defines no
Objective-C class, so a hosting plugin loaded twice in one DAW has nothing to
clash on.

## Building it

`MAKEASOUND_BUILD_HOSTING` (on for a top-level build, off as a dependency) needs
`MAKEASOUND_BUILD_PLUGIN`. The target builds on macOS and Windows for now; Linux
and iOS come later, and the per-platform files below are where they slot in.

There are to be two libraries:

| target | holds | links |
| --- | --- | --- |
| `MakeASoundHosting` | formats, scanning, the cache, loading and processing | `MakeASoundPlugin`, `vst3sdk-hosting`, eacp-core, AudioToolbox on macOS |
| `MakeASoundHostedEditor` | a hosted plugin's own editor in an eacp view | the above, eacp-graphics, Cocoa and CoreAudioKit on macOS |

The split keeps a scanner's worker process, which loads a plugin only to describe
it, free of the graphics stack.

### `makeasound_enable_hosting(<target>)`

Turns a target that already exists, an app or a plugin, into a host:

```cmake
add_executable(MyHost Main.cpp)
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
| macOS | yes | yes (v2, and v3 through the v2 API) | yes | yes | yes (VST3, AU) |
| Windows | yes | - | yes | - | yes (VST3, HWND child, DPI conversion) |
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
  platform in `vst3sdk-hosting` (`ThirdParty/CMakeLists.txt`), and none on
  iOS, where `MakeASoundHosting` is not built

Every VST3 TU includes `Plugin/VST3/VST3Common.h` first, as the format adapter's
do.

## What is not here yet

The rest of Tamber Plug's hosting layer moves here in steps. Waiting:

- **Windows hosted-editor tests** (pixels and points against a DPI): the code is
  in, built and tested on macOS only so far.
- A typing keyboard in `Apps/PluginHost`.
