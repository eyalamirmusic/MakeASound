# Plan: a plugin framework on MakeASound

MakeASound grows from a device library into a write-once plugin framework: one
`Plugin` compiled to VST3, AU and a standalone app, later CLAP and AUv3. The design
follows Plug (the format-neutral core, thin per-format adapters, eacp for
windows and editors, Miro for state and UI schemas), re-homed on MakeASound so the
standalone format and the device library are one code base rather than two.

Branch: `plugin-framework`. Stages land in order; each has a provable end state.
Downstream consumers pin `gh:eyalamirmusic/MakeASound#main` (Plug, tamber-web), so
nothing merges to `main` until the sites listed under each stage are updated with
it.

| stage | deliverable | proof |
| --- | --- | --- |
| 0 | `Buffer`, one class, owning or referring | suite green, allocation tests cover every view path — **landed 2026-10-10** |
| 1 | `Processor`, `ProcessContext`, `Engine` in the device library | the Synth demo is a `Processor` run by `Engine` — **landed 2026-10-10** |
| 2 | `MakeASoundPlugin`: `Plugin`, parameters, state, description | fake-host tests drive a plugin end to end — **landed 2026-10-10** |
| 3 | Standalone format | a `Plugin` runs in a window with device and MIDI pickers — **landed 2026-10-10** |
| 4 | VST3 | pluginval at strictness 10 — **landed 2026-10-10** |
| 5 | AU | `auval -strict` on both examples after an install step — **landed 2026-10-10** |
| 6 | editors, examples, CI, docs | |

## Stage 0: `Buffer`

### Decision

One class, `MakeASound::Buffer`, that either owns its samples or refers to
samples owned by someone else, the way `juce::AudioBuffer` does, with one
difference that removes JUCE's main footgun: it is **move-only**. A copy is a
compile error, a deep copy is spelled `Buffer::copyOf(const Buffer&)`, and a move
is cheap in both states. Sub-ranges, channel subsets and `getOutput()` all return a
referring `Buffer` by value through guaranteed elision, so `auto out =
info.getOutput()` keeps working and never allocates.

Why one class rather than an owner plus a view: a single name and a single API,
`void process(Buffer&)` everywhere, and familiarity for anyone coming from JUCE.
Why move-only rather than JUCE's deep copy: the library's own `auto` convention
would otherwise turn every `auto out = ...` into an allocation on the audio thread.

### Layout

```cpp
class Buffer
{
    float* const* channels = nullptr;   // the table every access goes through
    int startSample = 0;                // added on every access
    int numChannels = 0;
    int numSamples = 0;

    Vector<float> samples;              // engaged only when owning
    Vector<float*> table;               // the table `channels` points at when owning
};
```

A referring buffer points at a table someone else owns: a host's `float**`, an
owning `Buffer`'s table, the miniaudio backend's scratch. The start offset is what
makes a sub-range the same type with no storage of its own and no channel cap:
`getChannel(c)` is `channels[c] + startSample`, and sub-ranges compose by adding
offsets. This is `juce::dsp::AudioBlock`'s strategy, not `juce::AudioBuffer`'s
(which bakes the offset into a copied table and so needs 32 inline pointers plus a
heap fallback).

An owning buffer lays channels out channel-major in one allocation, each channel
padded to a multiple of 16 floats so a SIMD path can assume alignment, and keeps
`table` pointing into it. `setSize` zeroes and never releases capacity, so a
prepare-time `setSize` followed by equal-or-smaller ones is allocation-free.

### API

```cpp
class Buffer
{
public:
    Buffer() noexcept = default;
    Buffer(int numChannels, int numSamples);                        // owning, zeroed
    Buffer(float* const* channels, int numChannels,
           int numSamples, int startSample = 0) noexcept;           // referring

    Buffer(Buffer&&) noexcept;
    Buffer& operator=(Buffer&&) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    static Buffer copyOf(const Buffer& source);                     // owning

    // Gives the buffer storage of its own, dropping anything it referred to.
    void setSize(int numChannels, int numSamples);
    // Drops owned storage and refers instead.
    void referTo(float* const* channels, int numChannels,
                 int numSamples, int startSample = 0) noexcept;

    bool isOwning() const noexcept;
    bool isEmpty() const noexcept;
    int getNumChannels() const noexcept;
    int getNumSamples() const noexcept;

    Channel getChannel(int channel) noexcept;
    ConstChannel getChannel(int channel) const noexcept;
    Channel operator[](int channel) noexcept;
    ConstChannel operator[](int channel) const noexcept;
    float* getChannelPointer(int channel) noexcept;
    const float* getChannelPointer(int channel) const noexcept;

    // The table and the offset it is read through; for a C API.
    float* const* getChannelPointers() const noexcept;
    int getStartSample() const noexcept;

    // Referring buffers over part of this one. Clamped to its shape.
    Buffer getSubBuffer(int start, int numSamples) noexcept;
    Buffer getSubBuffer(int start) noexcept;
    Buffer getChannelSubset(int firstChannel, int numChannels) noexcept;
    Buffer getSingleChannel(int channel) noexcept;

    void clear() noexcept;
    void fill(float value) noexcept;
    void copyFrom(const Buffer& other) noexcept;              // shared shape only
    void addFrom(const Buffer& other, float gain = 1.f) noexcept;
    void applyGain(float gain) noexcept;

    Iterator begin() noexcept;          // yields Channel; shape held by value
    Iterator end() noexcept;
    ConstIterator begin() const noexcept;
    ConstIterator end() const noexcept;
};

using Channel = Span<float>;
using ConstChannel = Span<const float>;
```

Semantics worth pinning:

- **Constness lives in the reference**, as in JUCE. `const Buffer&` hands out
  `ConstChannel`. A buffer over genuinely const host memory needs a `const_cast`
  at the wrapper, and there are no const overloads of the sub-range methods: a
  `const Buffer` cannot be sliced, because the slice would be writable. The
  wrapper slices before it lends.
- **`setSize` on a referring buffer makes it owning**; `referTo` on an owning one
  frees. Both are defined transitions, not errors, and neither belongs on the audio
  thread. `isOwning()` exists for the rare caller who needs to ask.
- **Moving an owning buffer** leaves the source empty and not owning. Moving a
  referring one copies the four view fields.
- **Nothing in the type says "never allocates"**; the referring constructors,
  the sub-range methods and every accessor are `noexcept` and allocation-free, and
  `Tests/AllocationTests.cpp` asserts it under the thread-local ban.
- **No `isClear` flag, no gain ramps, no RMS.** DSP belongs on `Channel`, in
  `Algorithms`, or in ESIMD later; a container should not become a DSP library.
- **No flat-span constructor.** Viewing a contiguous `float*` as channels needs a
  table beside it; the owning constructor is the replacement.

### Fallout in this repository

| place | change |
| --- | --- |
| `Audio/Buffer.{h,cpp}` | rewritten; `.cpp` is new and holds the allocating members |
| `Audio/Channel.h` | gains `ConstChannel` |
| `Common/Common.h` | drops the `PlanarView` re-export; nothing uses it afterwards |
| `Devices/DeviceInfo.h` | `AudioCallbackInfo` loses `inputBuffer`/`outputBuffer` (flat pointers) for `inputChannels`/`outputChannels` (tables the backend owns); `getInput()`/`getOutput()` return referring `Buffer`s by value; `numInputs`/`numOutputs`/`numSamples` stay, since every consumer reads them |
| `MiniAudio/MiniAudioDeviceManager` | the two `Vector<float>` scratches become two owning `Buffer`s sized at open from `maxBlockSize` and re-sized in the callback only if a larger block ever arrives, as today; de-interleave writes per channel through the table |
| `Devices/DeviceManager.cpp` | `prevInfo` unchanged: `AudioCallbackInfo` stays a copyable POD |
| `Tests/BufferTests.cpp` | rewritten: owning, referring, sub-range, subset, move, `copyOf`, `setSize` capacity, iteration off a temporary |
| `Tests/AllocationTests.cpp` | the callback case builds its table; new cases: every view operation under the ban, `setSize` to an equal or smaller shape under the ban |
| `Tests/DeviceInfoTests.cpp` | the `getInput`/`getOutput` case builds tables |
| `Tests/RealtimeThreadAllocationTests.cpp`, `Apps/*`, `README.md` | `info.getOutput().channels()` becomes `info.getOutput()`; the strided and flat-span spellings go |
| `CLAUDE.md` | the `Audio/` paragraph describes the new class |

### Fallout downstream

Plug (`~/Code/Plug`): nothing. `Standalone/Engine.cpp` and
`Examples/TransposeDemo` use `getOutput()`, `getInput()`, `getChannelPointer`,
`numInputs`, `numOutputs`, `numSamples`, `sampleRate`, `dirty`; all keep their
spelling.

tamber-web (`~/Code/tamber-web`): two lines.
`apps/librarian/SamplePlayer.cpp:184` iterates `output.channels()` and should
iterate `output`; `lib/tamber_tamby/AudioProcessor.cpp:96` wraps a flat
`Vector<float>` with the flat-span constructor and should resample into a
one-channel owning `Buffer` member instead.

### Done when

- the full suite is green on macOS, including both allocation suites;
- `MakeASound::Buffer` has no copy constructor and the compile fails on one;
- `Apps/Synth`, `Apps/Demo`, `Apps/Example`, `Apps/AudioProbe` build and run;
- README and CLAUDE.md describe the class as it is.

## Stage 1: `Processor`, `ProcessContext`, `Engine`

The format-neutral processing vocabulary, in the device library, so an app can
use it with no plugin involved and so the standalone format is nearly free.

- **`Audio/BusLayout.h`**: `Bus {name, numChannels, isMain}`, `MidiBus {name}`,
  `BusLayout {inputs, outputs, midiInputs, midiOutputs}` with `stereoInOut()`,
  `stereoOut()`, `BusLayout::acceptsExactly`. Miro-reflected.
- **`Audio/Playhead.h`**: `isValid, isPlaying, isRecording, isLooping,
  sampleTime, ppqPosition, barStartPpq, bpm, TimeSignature, loopStart, loopEnd`.
- **`Audio/ProcessContext.h`**: `ProcessSpec {int sampleRate; int maxBlockSize;
  BusLayout layout;}` and `ProcessContext {Vector<Buffer> inputs, outputs;
  Vector<MIDI::Buffer> midiIn, midiOut; Playhead playhead; ParameterChanges
  paramChanges;}` with `mainInput()`, `mainOutput()`, `mainMidiIn()`,
  `mainMidiOut()`. The inputs are referring `Buffer`s over memory the host owns;
  the context reserves its vectors at prepare time and never grows after. Sample
  rates stay `int` for consistency with `DeviceManager`; the VST3 and AU adapters
  round at the boundary. `ParameterChanges` is empty in stage 1 and exists so
  sample-accurate automation is additive later.
- **`Audio/Processor.h`**: `getBusLayout()`, `prepare(const ProcessSpec&)`,
  `process(ProcessContext&) noexcept`, `reset() noexcept`. Plug has no `reset` and
  fakes one in its AU adapter; a real virtual is cleaner. The layout lives here
  rather than on `Plugin` because `Engine` has to size the context for an app-level
  processor with no plugin in sight; `Plugin` inherits it.
- **`Devices/Engine.{h,cpp}`**: wires a `Processor` to a `DeviceManager` and a
  `MidiManager`: `Engine(DeviceManager&, MidiManager&)`, `prepare(Processor&, rate,
  block)`, `start(const StreamConfig&, Processor&)`, `stop()`, and `process(
  AudioCallbackInfo&)` public so a test or a host with its own stream can drive it.
  It owns the `MidiBlockSync`, the `ProcessContext` and the stand-in buffers for a
  bus the device cannot feed, and renders the context over `AudioCallbackInfo`'s
  buffers with no copy. `start` prepares for the config, opens the stream, and if
  the device settled on another rate or period re-prepares on the host thread and
  re-opens; a `dirty` block resets the processor and the MIDI window, and only one
  whose shape still differs from the spec re-prepares on the audio thread. MIDI
  out buses are cleared and ignored until the standalone format brings its sender
  thread. This is most of `Apps/Synth/AudioProcessor.h` moved into the library.

Proof: the Synth demo becomes a `Processor` run by `Engine`, with its
`AudioProcessor.h` deleted; `Tests/EngineTests.cpp` drives a fake processor with
a synthetic `AudioCallbackInfo` and asserts prepare-on-dirty, MIDI alignment and
zero allocations in the steady state.

Landed: `Synth` is a `Processor` with `BusLayout::instrument()`, `SynthHost` holds
the managers, the `Engine` and the config the UI shows; twenty `Engine/` cases plus
two allocation cases; README and CLAUDE.md describe the vocabulary.

## Stage 2: the plugin core, `MakeASoundPlugin`

A second static target, `Lib/MakeASound/Plugin/`, SDK-free, linking
`MakeASound` and `eacp-core`. Namespace `MakeASound`; per-format adapters live in
`MakeASound::VST3`, `MakeASound::AU`, `MakeASound::Standalone`, the way device
backends live in `MakeASound::MiniAudio` and `MakeASound::CoreMIDI`.

- **`Plugin : Processor`** adds `name()`, `acceptsLayout()`,
  `createEditor()`, `latencySamples()`, `tailSamples()`, `saveState()` /
  `loadState()` with a `StateContext` (session vs preset), and the host bridge
  `HostEditListener` (begin/perform/end edit, latency changed, parameter info
  changed).
- **Parameters.** `Param<float>`, `ChoiceParam`, `BoolParam`, plus the typed
  helpers Plug has (`DecibelParam`, `HzParam`, `TimeParam`, `PercentParam`).
  Declared in a `ParameterGroup` whose constructor registers its members with
  `add(...)`; nested groups become id and display prefixes, ids default to names
  with an `id` override, and the group's own `reflect` serialises state. The host id is a 32-bit hash of the stable string
  id, collision-asserted at registration and overridable, not the registration
  index as in Plug: inserting a parameter must not break saved automation, and
  CLAP requires stable ids anyway. Values are `std::atomic`, read directly on the
  audio thread; `Realtime/Smoother.h` is a helper, not a framework concern. Stage
  2 takes the last automation point per block, as Plug does.
- **State.** Miro JSON with a `version` field and tolerant loading, choices by
  name, a session-only tier, and a published snapshot so a host can save off the
  message thread (Plug's `auval -strict -stress` deadlock is why).
- **Description.** `ModuleDescription {vendor, url, email, version,
  manufacturerCode, plugins}` and `PluginDescription {name, version, category,
  pluginCode, create}` from one `describeModule()` the plugin TU defines.
- **`PluginWrapper`**, the per-block pipeline every format shares: copy each input
  bus into its output bus unless aliased, zero the extras, clear and sort MIDI,
  reconcile parameters, `process`, drain MIDI out, under `ScopedNoDenormals`.
- **Realtime additions**: `Realtime/RealtimeSwap.h` (GUI-to-realtime swap with
  message-thread reclamation) and `Realtime/Smoother.h`.

Proof: `Tests/PluginTests.cpp` drives a `Plugin` through `PluginWrapper` with a
fake host: layout negotiation, parameter ids and hashing, state round trips
including a schema with a parameter inserted mid-list, in-place and aliased
buses, MIDI ordering, and allocation-free steady state.

Landed: `MakeASoundPlugin` is a second static target behind
`MAKEASOUND_BUILD_PLUGIN` (on by default in a top-level build only, like
`MAKEASOUND_BUILD_APPS`, so Plug and tamber-web fetch no eacp unless they ask),
umbrella
`<MakeASound/Plugin/MakeASoundPlugin.h>`, linking `MakeASound` PUBLIC and
`eacp-core` PRIVATE; eacp is included by one TU,
`Plugin/Realtime/MessageThread.cpp`, and by no header. It holds `Plugin`,
`StatePlugin`, the parameter types, `ParameterGroup` and `ParameterList`,
`State`, `StateContext`, `HostEditListener`, `Description` and `PluginWrapper`;
`ScopedNoDenormals` and `Smoother` went into the device library's `Realtime/`.
Tests: 32 `Plugin/` cases over `Tests/TestPlugins.h` (a `StatePlugin` gain effect
and a MIDI-echoing instrument), 19 `Parameters/`, 21 `State/`, 6 `RealtimeSwap/`,
10 for the smoother and the denormal guard, 6 in `PluginAllocationTests.cpp` and
one more in `AllocationTests.cpp`. README and CLAUDE.md describe the core.
Where it differs from the bullets above:

- **`FloatParam`, not `Param<float>`.** Each concrete type is a class over its
  own atomic (a `float`, a choice index, a `bool`) with its own text and state
  conversions, so a template would have covered only the first; the typed
  helpers derive from `FloatParam`. Options are a trailing `ParameterOptions`
  written with designated initialisers (`{.sessionOnly = true}`, `{.hostId = 7}`).
- **Parameters are registered, not reflected.** A params struct derives from
  `ParameterGroup` and calls `add(...)` in its constructor with parameters,
  groups, pointers to either and ranges of any of those; it carries no
  `MIRO_REFLECT`, and a `Parameter` in a `MIRO_REFLECT` list elsewhere does not
  compile. A group is named per instance (`OscParams osc1 {"Osc 1"}`), an id
  defaults to the name (`"Osc 1/Attack"`, displayed `"Osc 1 Attack"`) and
  `ParameterOptions::id` / `GroupOptions::id` pin one across a rename. A range
  adds no segment: each element is under its own name. The group's `reflect`
  saves and loads by walking what was registered, so a load assigns into the
  existing parameters and a vector of groups survives it; see the decisions log.
- **Host ids are 31 bits.** `hostIdFor` masks the FNV-1a hash with `0x7fffffff`
  because VST3 reserves ids from 2^31 up, and an explicit `hostId` at or above
  that asserts.
- **`createEditor` is deferred to stage 3.** There is no `Editor` type yet, and
  stage 3's window is the first thing that needs one; `Plugin` carries no
  placeholder.
- **`RealtimeSwap` is redesigned and lives in `Plugin/Realtime/`**, not the device
  library's `Realtime/`, because it needs the message thread. Plug's FIFO plus a
  `use_count` reaper is replaced by two atomic slots, `pending` and `retired`,
  with single ownership: the audio thread takes `pending` at the top of a block
  and parks what it held in `retired`, and a process-wide reclaimer in
  `MessageThread.cpp` frees every registered swap's `retired` on the message
  thread every 250 ms. While `retired` is still full the audio thread keeps its
  object one more block, so the latest publish is delayed, never lost, and no
  `shared_ptr` count is ever touched on the audio thread.
- **Parameter reconciliation is the adapters'.** `ProcessContext::paramChanges`
  is still empty; an adapter writes each host point through
  `setNormalizedParameter`/`setParameterByHostId` before the block, so the last
  point per block wins as planned, but the wrapper does not walk a queue.
  `holdParameter`/`releaseParameter` count a plugin-side gesture, and the
  wrapper's three host setters drop a write while the parameter is held: the
  editor sets the `Parameter` directly and reports through `HostEditListener`.
- **The wrapper is a pipeline of calls, not one call.** The adapter runs the
  steps in order (parameters, `setPlayhead`, `clearMidi`/`pushMidiIn`/
  `sortMidiInByOffset`, `bindInput`/`bindOutput` per bus, `process`, drain
  `midiOut()`); `process()` itself only clears MIDI out (`ProcessContext` gained
  `clearMidiOut()`), runs the plugin under `ScopedNoDenormals` and detaches
  every audio bus afterwards, so a bus left unbound next block reads empty
  rather than the last block's host pointers. `bindOutput`
  decides per channel, not per bus (copy, skip when aliased, zero past the
  input's width), since zeroing an output that aliases an input would wipe it.
  Out-of-range indices are no-ops and a full MIDI bus drops the event.
- **More on `Plugin` than listed:** `format()`, set by the wrapper's constructor
  argument after the plugin is built (so `Unknown` in its constructor, valid
  from `prepare`); `takeLatencyChanged()`; and three state hooks beside
  `saveState`/`loadState`: `saveStateWithoutMessageThread`,
  `isStateSnapshotCurrent`, `loadParameters` and `loadStateExceptParameters`
  (whose default loads the whole document, for a plugin that cannot split it). `HostEditListener` gained an
  optional edit group (VST3 only).
- **The threading of state is the wrapper's.** `saveState` off the message thread
  returns the plugin's snapshot and marshals and waits only when there is none,
  with a throwing `saveState` handed back through the promise. `State` publishes
  a preset and a session document, so a `StatePlugin` never marshals.
  `loadState` applies the parameters inline (they are atomics) and the rest of
  the document on the message thread later, without the parameters, so a host
  write in between survives; each load is stamped from an atomic counter, and a
  deferred half older than the latest load drops, as does one queued past
  teardown. The wrapper is destroyed on the message thread (asserted), where a
  running deferred load holds the plugin. `isPublishedDocumentCurrent` compares
  with `params` erased, since automation moves them between any two reads.
- **`version` is written, `loadedVersion` is read.** A load never overwrites the
  schema version the build writes; the document's lands in `loadedVersion` for
  migration code in a `reflect` override under `ref.isLoading()`.
- **Session-only parameters** are written and read by `ParameterGroup::reflect`
  only under `StateContext::Session`, so a preset carries no key for them.
- **Description** gained `subcategory` and `Category::MidiEffect` (AU `aumi`, a
  VST3 instrument), and codes are a `FourCC` type.
- **The allocation-free steady state** is pinned in `PluginAllocationTests.cpp`
  rather than `PluginTests.cpp`, because it needs the interposer and so builds
  only on Apple and Linux.

## Stage 3: the standalone format, the first provable step

The first format, because it needs no SDK, exercises every MakeASound seam, and
is what Plug's `Standalone/` already proves works on top of this library.

- **`Editor` and `Plugin::createEditor()`**, deferred from stage 2: the smallest
  type the window can host (a `view()`), with size policy and host resize left
  to stage 6. A plugin that returns none gets the generic parameter page, built
  from `parameters()`' display names and `valueToText`.
- **`Plugin/Standalone/`**: `StandaloneApp` built on `eacp::Apps::run<T>`, a
  `Window` whose content is the editor's view or a generic parameter page, a
  `PluginWrapper` constructed with `PluginFormat::Standalone` and no
  `HostEditListener` (there is no host), `Engine` from stage 1 driving it, a
  settings panel built from `UIDeviceManager` and `UIMidiManager`'s dropdowns
  and toggle lists, settings persisted as the already-reflected `StreamConfig`
  plus open MIDI ports in `FilePath::appSupportDirectory()`, a typing keyboard
  feeding MIDI through an `SPSCQueue`, and state saved on quit and restored on
  launch through the wrapper's `saveState`/`loadState` with
  `StateContext::Session`, on the message thread. `Engine` runs a `Processor`,
  not a `PluginWrapper`, so the adapter needs a thin `Processor` that drives the
  wrapper's `bind*`/`process` steps, or `Engine` grows a path for the wrapper;
  decide at the start of the stage.
- **Build**: `makeasound_add_plugin(<Name> FORMATS Standalone SOURCES ...)` in
  `CMake/MakeASoundPlugin.cmake` makes the static core `<Name>` and
  `<Name>-Standalone` (`MACOSX_BUNDLE`, `eacp_set_gui_subsystem` on Windows,
  ad-hoc codesign). The function is written so VST3 and AU slot in as further
  `FORMATS` without changing its signature.
- **Examples**: `Plugins/Gain` (effect, the smallest possible) and
  `Plugins/Synth` (instrument, ported from `Apps/Synth`).

Proof: both examples run as standalone apps on macOS with device, sample rate,
block size and MIDI port pickers; `Synth` plays from a hardware MIDI port and the
typing keyboard; settings and state survive a relaunch; the per-example allocation
test (the harness from `Tests/AllocationProbe.h`) passes under a live callback.

Landed: two more static targets, each in its own directory's `CMakeLists.txt`
under `Plugin/CMakeLists.txt` and gated on the eacp target it links existing
(`eacp-ui` for the UI library, `eacp-graphics` on a desktop for the standalone),
so a Linux or iOS build keeps the core and skips the window. `MakeASoundPluginUI` (`Plugin/UI/`, linking
`MakeASoundPlugin` and `eacp-ui` PUBLIC) holds `GenericEditor`;
`MakeASoundStandalone` (`Plugin/Standalone/`, linking `MakeASoundPluginUI` and
`eacp-graphics` PUBLIC) holds `StandaloneApp`, `StandaloneProcessor`,
`MidiSender`, `TypingKeyboard`, `Settings` and `SettingsPanel`.
`makeasound_add_plugin(<Name> FORMATS Standalone SOURCES ... [OUTPUT_NAME]
[BUNDLE_ID] [COMPANY])` lives in `CMake/MakeASoundPlugin.cmake`, which
`Plugin/CMakeLists.txt` includes, so a CPM consumer calls it from its own tree;
it tests `TARGET MakeASoundStandalone`, global for the same reason. It builds the sources once as the static core
`<Name>` and links `<Name>-Standalone` from
`Plugin/Standalone/StandaloneMain.cpp` (`eacp::Apps::run<StandaloneApp>()`), a
`MACOSX_BUNDLE` with `NSMicrophoneUsageDescription` and an ad-hoc codesign
post-build; without a GUI the format is skipped with a status line, and an
unknown format is a configure error. `MAKEASOUND_BUILD_EXAMPLES` (on, top-level
only, and only with the plugin core) adds `Plugins/Gain` (targets `Gain`,
`Gain-Standalone`, bundle `MakeASound Gain.app`) and `Plugins/Synth`
(`SynthPlugin`, `SynthPlugin-Standalone`, `MakeASound Synth.app`; a monophonic
last-note-priority instrument with a waveform choice, attack, release, level and
legato). Tests: 24 `Standalone/` cases in `StandaloneTests.cpp` (the processor
on `Engine` with a synthetic callback, injection order and capacity, the sender
over a virtual-port loopback, the typing keyboard's map, the settings file and
the by-name re-resolution), and in `StandaloneAllocationTests.cpp` a
`StandaloneProcessor` block on `Engine` for the effect and for the instrument
with an injected note echoed into the sender, plus a live callback on the
machine's default output device, banned from inside the plugin's `process` and
measuring nothing where no device opens. README and
CLAUDE.md describe the format.
Where it differs from the bullets above:

- **`Engine` keeps running a `Processor`.** `Standalone::StandaloneProcessor` is
  the thin adapter the bullet offered: `getBusLayout()` is the wrapper's,
  `prepare` sizes one run of channel pointers per bus, back to back, and prepares
  the wrapper, and each `process` drives the wrapper's steps as any adapter will
  (`clearMidi`, the hardware MIDI of every bus, the injected queue at offset 0,
  `sortMidiInByOffset`, `setPlayhead`, `bindInput`/`bindOutput` through those
  tables, `process`, MIDI out bus 0 into `MidiSender`). `Engine` lives in the
  device library and cannot name `PluginWrapper`, and growing a path for it there
  would have pulled the plugin core into the device library. A run is clamped to
  what `prepare` sized, so a context wider than the layout binds fewer channels
  instead of writing past the table.
- **The host-side UI is eacp-ui GPU widgets, not web pages.** `GenericEditor` and
  `SettingsPanel` are `eacp::UI::ComponentHost` trees, the panel's lists built
  from the existing `UI::DropdownInfo` helpers (`makeOutputDeviceDropdown`,
  `makeSampleRateDropdown`, `makeBlockSizeDropdown`, the channel dropdowns), as
  `Apps/AudioProbe` already does: no npm, no JS bridge, no embedded resources in
  a plugin that never asked for them. A plugin's own `Editor` may still wrap a
  `WebView`. Stage 6's "generic parameter page with no npm" is therefore done.
- **`Editor` is `view()`, `initialSize()`, `isResizable()`,
  `onAttached()`/`onRemoved()`**, with `view()` an `eacp::Graphics::View&`
  built lazily and stable for the editor's lifetime. `Editor.h` carries only a
  forward declaration of `eacp::Graphics::View`, so `MakeASoundPlugin` still
  includes no eacp header and links no GUI tier; whoever builds or hosts an
  editor does. `Plugin::createEditor()` returns null by default, and is cheap by
  contract, since a host may call it only to learn whether one exists.
- **`MakeASoundPluginUI` is a target of its own**, not part of the standalone:
  `GenericEditor` is what every format shows for a plugin with no editor, so
  VST3's and AU's views will link it without the standalone's device and window
  code.
- **MIDI out has a sender.** `MidiSender` owns an `SPSCQueue<MIDI::Event, 1024>`
  the audio thread pushes into and a thread that drains it every millisecond,
  sending while an output is open and dropping otherwise; it is stopped around
  opening or closing the output, because `MidiManager` does not guard a send
  against either. Only bus 0 is forwarded, to the one output port.
- **Settings are stored by name.** `Standalone::Settings {version, audio,
  midiInputPorts, midiOutputPort, pluginState}`: the `StreamConfig`, the port
  *names* and the plugin's `Session` document, at
  `<app support>/<vendor>/<plugin>/settings.json`, written atomically.
  `resolveConfig` re-points each saved side at the device of that name present
  now (its channel span clamped, a missing device taking the fallback's side, a
  rate no device lists re-chosen as `pickCompatibleSampleRate` would), and
  `resolvePortIds` maps port names to today's ids, because both id registries
  are per launch. A missing, blank or non-object file loads as nothing.
- **The typing keyboard** is Ableton's layout (`A W S E D F T G Y H U J K O L P
  ;` over 17 semitones, `Z`/`X` an octave) on eacp `KeyCode`s, a pure class with
  a sink that the app points at `StandaloneProcessor::injectMidi` (an
  `SPSCQueue<MIDI::Event, 256>`, landing at offset 0 after the hardware events of
  that offset). It remembers the note each key holds, so a release after an
  octave shift ends the note that was played. The app feeds it from a
  `WindowInputListener` on both windows, ignores a repeat and a key with
  command, control or alt held, releases every held note when a window stops
  being key, and turns it on by default for a layout with a MIDI-in bus, with
  Cmd+K (`Computer MIDI Keyboard` in the app menu) toggling it.
- **`StandaloneApp` hosts the module's first plugin** and has no plugin picker.
  The editor window is the primary one (resizable only when the editor says so);
  the settings window is a hidden secondary one opened by Cmd+, and hidden again
  on close. The app menu also has `Reset Plugin to Defaults` (the `Preset`
  document saved before any settings were loaded, so session-only parameters
  stay) and `Reset Audio / MIDI Settings` (every port closed, the default
  config). The default config is output-only for every layout: an instrument
  asks for no microphone, and an effect's input is only ever one the user picked
  in the settings window, because the default microphone into the default
  speakers is a feedback loop (which the first run of `Gain` produced). A failed
  open is printed to `stderr`; the app keeps running with what it has.
- **`SynthPlugin`, not `Synth`**, for the instrument example's targets:
  `Apps/Synth`, the `Engine` demo it was ported from, owns that name and stays.
- **One synth, two hosts** (2026-10-10): the instrument's DSP moved out of the
  plugin into `MakeASoundDSP`, an optional static target under
  `Lib/MakeASound/DSP/` that links the device library alone, as
  `DSP::TestSynth`, a `Processor` with settable `Settings`. `Plugins/Synth` is
  that synth with its settings on parameters and `Apps/Synth` is it behind the
  web page's gain slider and MIDI log, so the demo and the plugin play the same
  instrument. `Tests/TestSynthTests.cpp` covers it and the allocation suite
  drives a block of it.
- **The settings panel follows the layout.** `SettingsPanelOptions::forLayout`
  hides the input rows for a layout with no input bus, the MIDI input toggles for
  one with no MIDI-in bus and the MIDI output picker for one with no MIDI-out bus;
  the panel edits a copy of the config, reports it, and shows what the app
  answers with `setConfig`, and a 2 Hz timer of its own rebuilds the lists when a
  device or port comes or goes.
- **The proof's allocation test is the format's, not each example's**: the
  standalone cases in `StandaloneAllocationTests.cpp` drive `StandaloneProcessor`
  with the test plugins, which cover what both examples exercise.

## Stage 4: the VST3 format

The first format a DAW loads. It is a thin adapter over `PluginWrapper` and the
vendored SDK. The proof is pluginval at strictness 10 on both example bundles, on
every desktop CI platform pluginval ships for.

### Decision

There is one **single-component** class per plugin, `MakeASound::VST3::Adapter`. It
derives from `Steinberg::Vst::SingleComponentEffect` (which supplies `IComponent`,
`IAudioProcessor`, `IEditController`/`EditControllerEx1` and
`IProcessContextRequirements`), plus `IMidiMapping` and `HostEditListener`. It owns a
`PluginWrapper` built with `PluginFormat::VST3`. Like `StandaloneProcessor`, it turns
host types into the wrapper's per-block calls in their documented order and never
calls the plugin's `process`.

The design follows what Plug proved, mapped onto MakeASound's types:
- **CC is delivered through `IMidiMapping` and hidden shadow parameters**, expanded
  point by point into `MIDI::Event`s.
- **State goes through the wrapper's `saveState`/`loadState`.**
- **The edit gate** is `holdParameter`/`releaseParameter`.
- **Latency change** is reported through a compare-exchange on the last figure the
  host fetched.
- **Bus arrangements** are negotiated through `PluginWrapper::setLayout`.

Five places where this deliberately departs from Plug:

- **`ParamID` is the parameter's host id** (`ParameterList::Entry::hostId`, a 31-bit
  FNV hash), not its index. Lookups go through `ParameterList::indexOfHostId`, a
  binary search that never allocates. The SDK's `ParameterContainer` is only for
  host-facing info.
- **Controller values are read live.** `getParamNormalized` is overridden to return
  `wrapper.getNormalizedParameter(index)`. The controller's cache can never go stale,
  so Plug's `syncControllerParameters` after a load is gone.
- **`ProxyParameter` derives from `Vst::Parameter`, not `RangeParameter`.**
  `toPlain`/`toNormalized`/`toString`/`fromString` all go through the MakeASound
  `Parameter`, so skewed parameters (`HzParam`, `DecibelParam`) show and convert
  correctly in the host.
- **Shadow ids stay below 2^31.** Plug set bit 31, which VST3 reserves. Here they
  take the top 2^16 ids of the 31-bit space.
- **No `kDistributable` class flag.** A single-component plugin must not claim its
  processor and controller can run on different machines.

**State.** One document, carried by `IComponent::getState`/`setState`.
- `IEditController::getState`/`setState` are left at the SDK default,
  `kNotImplemented`. Inside `SingleComponentEffect` they are spelled
  `getEditorState`/`setEditorState`, because the SDK renames them with a macro. The
  plugin has no UI-only state, and storing the document twice would let the two
  copies disagree.
- **Context.** The document is a `StateContext::Session` unless the host's stream
  answers `IStreamAttributes` with a `PresetAttributes::kStateType` other than
  `StateType::kProject`; then it is a `Preset`. So a host that marks preset saves
  leaves session-only parameters out, and every other host gets the whole session.
- **Threading** is the wrapper's: `getState` off the message thread reads the
  published snapshot, and `setState` applies the parameters inline and defers the
  rest.
- `setComponentState` (the controller's copy of the component state) returns
  `kResultOk` without reading the stream. The component and the controller are one
  object, so the state is already applied.

**Shadow parameters for MIDI.**
- **When they exist.** They are registered when the layout has at least one MIDI
  input bus, with no flag. Without them a VST3 instrument receives no CC, pitch bend,
  aftertouch or program change at all, and a flag would mean a change to the
  `BusLayout` API.
- **Count.** 16 channels × 131 controllers (CC 0–127, `kAfterTouch` 128, `kPitchBend`
  129, `kCtrlProgramChange` 130) per MIDI input bus, for at most 16 buses: 2,096
  hidden parameters per bus. Plug's Synth shipped with the same count and passed
  pluginval.
- **Ids.** `0x7FFF0000 | bus << 12 | channel << 8 | controller`.
- **Collisions.** A plugin parameter whose host id lands in that range always wins.
  The shadow with the same id is neither registered nor mapped, and a debug build
  prints the parameter's id path and asserts, with ParameterList's "give one an
  explicit hostId" message. The odds are about 3×10⁻⁵ per parameter, it is
  deterministic for a given id string, and it is caught on the author's first debug
  run.

**Which parameters are registered.** Only entries for which
`ParameterList::isHostExposed(i)` is true (that is, automatable). Edits to the others
still hold and release the wrapper, but never reach
`beginEdit`/`performEdit`/`endEdit`, because hosts surface any parameter an edit
touches. State still carries them.

**Preparation happens in `setActive(true)`**, not `setupProcessing`. Bus arrangements
may change between `setupProcessing` and activation, so `wrapper.prepare` runs once
the layout is final. `setupProcessing` only records the setup, through the base
class.

**The message thread inside a DAW.**
- `MessageThread` gains `adoptHostMessageThread()`. It calls
  `eacp::Threads::attachCurrentThreadAsMain()` when `eacp::Platform::isDLL()`. The
  factory's create function and `initialize` both call it. On Windows the module is
  otherwise loaded with the loader thread marked as main, and on Linux eacp's
  per-copy loop is otherwise never attached.
- **Linux pumping.** The eacp loop is pumped through the host's
  `Steinberg::Linux::IRunLoop` in two ways: module-wide, when the host hands one to
  `IPluginFactory3::setHostContext`, and per view, from the `IPlugFrame`. Both
  register eacp's loop descriptor with an `IEventHandler` that calls
  `pumpMessageLoop()`.
- **Known Linux limitation.** In a host that offers neither, deferred work runs only
  while a view is open. Neither example needs it: both are `StatePlugin`s, so saves
  read the snapshot, and loads made on the UI thread run inline.

**Where the view lives.** There is one target, `MakeASoundVST3`, always built where
`vst3sdk` exists on a desktop. It compiles `PlugView.cpp` and links
`MakeASoundPluginUI` when that target and `eacp-graphics` exist; otherwise it
compiles `NoPlugView.cpp`. Both TUs define the same free function, `createPlugView`,
and the choice is made at link time inside the target.

The alternative was a separate UI target registering a factory from a static
initialiser, which is Plug's `tamberplug_vst3_editor`. That needs `WHOLE_ARCHIVE` to
survive the linker and breaks silently when it does not. A view is not optional per
plugin either: every VST3 plugin shows `createEditor()` or `GenericEditor` wherever
eacp can draw. `MakeASoundPlugin` is untouched; only the adapter target sees eacp-ui,
and no adapter header includes an eacp header.

**Class id.** `FUID(manufacturerCode, pluginCode, FourCC("VST3"), 0)`, identical to
Plug's derivation. A plugin moved from Plug to MakeASound with the same codes is then
the same class to a DAW, so its saved sessions still find it. The id must never
change once shipped.

**Bundle metadata.** `Info.plist` is generated at configure time, so version, name
and identifier come from CMake arguments (`VERSION`, `OUTPUT_NAME`, `BUNDLE_ID`,
`COMPANY`), not from `describeModule()`. No VST3 host reads the plugin's identity
from the plist; it reads the factory, which reports `PluginDescription::version` from
C++. A plist that disagrees is therefore cosmetic.

AU differs: its `AudioComponents` list *is* read from the plist, which is why stage 5
has a build-time generator. The VST3 bundle id is `<BUNDLE_ID>.vst3`, one per format,
after Plug's lesson that the macOS Installer resolves package components by bundle
id.

### Layout

New directory `Lib/MakeASound/Plugin/VST3/` (namespace `MakeASound::VST3`, IDE folder
`Lib/Plugin`):

| file | what |
| --- | --- |
| `CMakeLists.txt` | the `MakeASoundVST3` target and its file properties (below) |
| `VST3Common.h` | the first include of every TU in the target. It includes `public.sdk/source/vst/vstsinglecomponenteffect.h` **before any other SDK header**: that header renames `IEditController::setState`/`getState` to `setEditorState`/`getEditorState` while it includes `ivsteditcontroller.h`, and an earlier include of that header defeats the rename. Also `namespace Vst = Steinberg::Vst;` and the `using` lines for `tresult`, `TBool`, `int32`, `uint32`, `FIDString`, `IBStream`, `FUnknown` |
| `Adapter.{h,cpp}` | the component |
| `HostParameters.{h,cpp}` | `ProxyParameter`, the shadow id scheme and shadow parameter names |
| `Conversion.{h,cpp}` | noexcept conversions: playhead, events in and out, shadow point to `MIDI::Event`, channel count to `SpeakerArrangement` |
| `Text.{h,cpp}` | UTF-8 ↔ `String128` through `Vst::StringConvert` |
| `Stream.{h,cpp}` | read and write an `IBStream` whole; the `StateContext` a stream asks for |
| `Factory.{h,cpp}` | `makeFactory(const ModuleDescription&)`, `classIdFor(...)` |
| `PlugViewFactory.h` | declares `createPlugView` |
| `PlugView.{h,cpp}` | the `IPlugView` over `eacp::Graphics::EmbeddedView` (UI build only) |
| `PlugView-{macOS,Windows,Linux}.cpp` | the platform's half of `PlugView`: `nativeViewType()`, `rectScale()`, `applyContentScale()` |
| `NoPlugView.cpp` | `createPlugView` returning null (no-UI build) |
| `HostRunLoop.h` | `HostRunLoop`, a pump alive as long as it is owned, and `attachHostRunLoop(FUnknown*)` |
| `HostRunLoop-Linux.cpp`, `HostRunLoop-Default.cpp` | an `IEventHandler` pumping eacp's loop from the host's `IRunLoop`; null elsewhere |
| `EntryPoint.cpp` | `GetPluginFactory`; compiled into each `<Name>-VST3` module, never into the static target |
| `VST3Info.plist.in` | the bundle plist template: `CFBundlePackageType` `BNDL`, identifier, name, versions, copyright, executable |
| `PkgInfo` | exactly the 8 bytes `BNDL????` with no newline. Plug's file is empty, a bug not to copy |
| `VST3Exports.map` | the Linux version script: `{ global: GetPluginFactory; ModuleEntry; ModuleExit; local: *; };` |

Elsewhere:

| file | what |
| --- | --- |
| `Lib/MakeASound/Plugin/Realtime/MessageThread.{h,cpp}` | gains `adoptHostMessageThread()`; `messageLoopFd()` and `pumpMessageLoop()` are in `MessageThread-Linux.cpp` and `MessageThread-Default.cpp` (additive) |
| `Lib/MakeASound/Plugin/CMakeLists.txt` | `add_subdirectory(VST3)` after `UI` and `Standalone` |
| `CMake/MakeASoundPlugin.cmake` | the `VST3` format and the `VERSION` argument |
| `CMake/InstallPluginBundle.cmake` | the best-effort `cmake -P` copy into the user's plug-in folder |
| `Lib/MakeASound/Plugin/Validation/` | the `MakeASoundPluginval` target: `Pluginval.{h,cpp}` (`fetch`, `validate`, `findBundles`), `PluginvalPlatform.h` and `Pluginval-{macOS,Windows,Linux}.cpp` (the release asset, the binary inside it, the chmod) |
| `Tools/PluginValidator/` | `Main.cpp` and `CMakeLists.txt`: the `PluginValidator` executable |
| `CMakeLists.txt` (top) | option `MAKEASOUND_INSTALL_PLUGINS` (ON); `add_subdirectory(Tools)` beside `Plugins` |
| `Tests/PluginvalTests.cpp` | the env-gated case over the example bundles |
| `Plugins/Gain/CMakeLists.txt`, `Plugins/Synth/CMakeLists.txt` | `FORMATS Standalone VST3` |
| `ThirdParty/VST3_SDK/public.sdk/source/vst/hosting/{parameterchanges,eventlist}.{h,cpp}` | vendored host helpers, for the tests only |
| `ThirdParty/CMakeLists.txt` | static target `vst3sdk-hosting` (those two `.cpp`s, linking `vst3sdk`) |
| `Tests/VST3TestHost.h` | `TestComponentHandler`, `TestAttributeStream` and the `AdapterHost` fixture, shared by both suites |
| `Tests/VST3Tests.cpp`, `Tests/VST3AllocationTests.cpp` | the suites |

**Targets:**

- **`MakeASoundVST3`** (STATIC).
  - Added only when `TARGET vst3sdk AND NOT IOS AND NOT ANDROID`; otherwise a status
    line and `return()`.
  - Sources: `Adapter.cpp`, `HostParameters.cpp`, `Conversion.cpp`, `Text.cpp`,
    `Stream.cpp`, `Factory.cpp`.
  - Links `MakeASoundPlugin` and `vst3sdk` PUBLIC.
  - With `TARGET MakeASoundPluginUI AND TARGET eacp-graphics`: adds `PlugView.cpp`
    and the platform's `PlugView-<OS>.cpp`, links both PRIVATE, and defines
    `MAKEASOUND_VST3_HAS_VIEW=1` PUBLIC so the tests know. Otherwise it adds
    `NoPlugView.cpp` and prints a status line.
  - Adds `HostRunLoop-Linux.cpp` on Linux and `HostRunLoop-Default.cpp` elsewhere.
    Platform code is split by TU, as eacp does, never by `#if`.
  - Properties: `MAKEASOUND_VST3_ENTRY`, `MAKEASOUND_VST3_PLIST`,
    `MAKEASOUND_VST3_PKGINFO`, `MAKEASOUND_VST3_LINUX_EXPORTS`, each the absolute
    path of the file of that name. They are read by `makeasound_add_plugin` from a
    consumer's directory, as `MAKEASOUND_STANDALONE_MAIN` already is.
  - Unity build follows `MAKEASOUND_UNITY_BUILD`; the `VST3Common.h`-first rule is
    what makes that safe. `set_makeasound_target_settings` applies.
- **`<Name>-VST3`** (MODULE, one per plugin), built by `makeasound_add_plugin` (see
  Build).
- **`vst3sdk-hosting`** (STATIC, test-only, IDE folder `External/VST3_SDK`).
- **`MakeASoundPluginval`** (STATIC), in `Lib/MakeASound/Plugin/Validation/`,
  added from `Plugin/CMakeLists.txt` after `VST3`, built when `TARGET eacp-network`
  exists on a desktop; links `MakeASound` and `eacp-network` PUBLIC.
- **`PluginValidator`** (executable, `Tools/PluginValidator/`, top-level builds
  with examples only, IDE folder `Tools`).

### API

```cpp
// MessageThread.h, additions
// Marks the calling thread as the message thread when this code lives in a
// dynamic library a foreign host loaded; a no-op in an executable. Idempotent.
// Host's UI thread only.
void adoptHostMessageThread();

// Linux: the descriptor the host's run loop watches for the message loop, -1
// elsewhere; and one non-blocking pass of that loop (a no-op elsewhere).
int messageLoopFd();
void pumpMessageLoop();
```

The `MessageThread*.cpp` TUs stay the only ones in the core that include eacp:
`MessageThread.cpp` takes `<eacp/Core/Platform/Platform.h>` for `isDLL`, and
`MessageThread-Linux.cpp` `EventLoop-Linux.h` for the descriptor and the pass;
`MessageThread-Default.cpp` answers -1 and does nothing.

```cpp
// HostParameters.h
inline constexpr uint32_t shadowIdBase = 0x7fff0000u;
inline constexpr int maxShadowBuses = 16;
inline constexpr int shadowControllers = 131;   // 0..127, kAfterTouch, kPitchBend, kCtrlProgramChange

struct ShadowTarget { int bus = 0; int channel = 0; int controller = 0; };

constexpr Vst::ParamID shadowIdFor(int bus, int channel, int controller) noexcept;
// nullopt unless id is in the range, controller <= 130 and bus < numMidiInputs.
std::optional<ShadowTarget> decodeShadowId(Vst::ParamID id, int numMidiInputs) noexcept;
// "MIDI CC 74 Ch 1", "Aftertouch Ch 1", "Pitch Bend Ch 1", "Program Change Ch 1",
// with " Bus 2" appended only when there is more than one MIDI input bus.
std::string shadowName(const ShadowTarget& target, int numMidiInputs);

// The host's view of one Parameter, which it never owns. Info is filled from
// the list entry: id = hostId, title = displayName, shortTitle = shortName(),
// units = label(), stepCount = numSteps(), defaultNormalizedValue =
// toNormalized(defaultValue()), unitId = kRootUnitId, flags = kCanAutomate |
// kIsBypass when isBypass() | kIsList when stateFormat() == Text (a choice).
class ProxyParameter : public Vst::Parameter
{
public:
    explicit ProxyParameter(const ParameterList::Entry& entry);

    // Re-reads title, units, steps and default for parameterInfoChanged().
    void refreshInfo();

    void toString(Vst::ParamValue normalized, Vst::String128 text) const override;
    bool fromString(const Vst::TChar* text, Vst::ParamValue& normalized) const override;
    Vst::ParamValue toPlain(Vst::ParamValue normalized) const override;
    Vst::ParamValue toNormalized(Vst::ParamValue plain) const override;

private:
    const ParameterList::Entry& entry;   // the list outlives the controller
};
```

Shadow parameters are plain `Vst::Parameter`s created with `shadowName`. Flags are
`kCanAutomate | kIsHidden`, step count 0, and the default is 0.5 for pitch bend and 0
otherwise.

```cpp
// Conversion.h, all noexcept and allocation-free
Playhead toPlayhead(const Vst::ProcessContext& context) noexcept;
bool fromVst3(const Vst::Event& in, MIDI::Event& out) noexcept;
bool toVst3(const MIDI::Event& in, int bus, Vst::Event& out) noexcept;
MIDI::Event shadowEvent(const ShadowTarget& target, int sampleOffset, float value) noexcept;
Vst::SpeakerArrangement arrangementFor(int numChannels) noexcept;
```

- **`toPlayhead`.** `isValid = true`. The transport flags come from `state`.
  `sampleTime = projectTimeSamples`. `ppqPosition`, `barStartPpq`, `bpm`,
  `timeSignature` and `loopStart/EndPpq` are each read only when their `k*Valid` bit
  is set; otherwise the default stays. This is Plug's `fromVst3`.
- **`fromVst3`** handles:
  - `kNoteOnEvent`, `kNoteOffEvent` and `kPolyPressureEvent`.
  - `kDataEvent` of type `kMidiSysEx` up to `MIDI::SysEx::maxBytes`, raw bytes as
    given; a longer one is dropped.
  - `kLegacyMIDICCOutEvent`, which some hosts send as input: CC, `kAfterTouch`,
    `kPitchBend` from `value`/`value2` as 14 bits, and `kCtrlProgramChange`.
  - Anything else returns false.
- **`toVst3`** is the inverse. Notes and poly pressure are native events with `noteId
  = -1`. CC, channel aftertouch, pitch bend (14 bits split over `value`/`value2`) and
  program change go out as `kLegacyMIDICCOutEvent`. A SysEx goes out as a
  `kDataEvent` whose `bytes` point into the wrapper's `midiOut()` storage, valid
  until the next `process()` clears it.
- **`shadowEvent`.**
  - Controllers 0–127 become `controlChange(channel, cc, value)`.
  - `kAfterTouch` becomes `channelAftertouch(channel, value)`.
  - `kPitchBend` becomes `pitchBend(channel, value * 2 - 1)`.
  - `kCtrlProgramChange` becomes `programChange(channel, lround(value * 127))`.
- **`arrangementFor`.** 0 gives `kEmpty`, 1 gives `kMono`, 2 gives `kStereo`, and n
  gives the first n speaker bits, `(1ull << n) - 1` (so 6 is `k51`).

```cpp
// Text.h
void copyTo(Vst::String128 destination, std::string_view utf8);   // truncates at 127
std::string toUtf8(const Vst::TChar* text);

// Stream.h
std::string readAll(IBStream& stream);           // until a read returns 0 bytes or fails
bool writeAll(IBStream& stream, std::string_view data);
StateContext stateContextOf(IBStream* stream);   // Session unless IStreamAttributes says a preset
```

```cpp
// Adapter.h
class Adapter : public Vst::SingleComponentEffect,
                public Vst::IMidiMapping,
                public HostEditListener
{
public:
    // Installs itself as the plugin's HostEditListener; the destructor removes it.
    explicit Adapter(OwningPointer<Plugin> plugin);
    ~Adapter() override;

    Plugin& plugin() noexcept;              // for the tests and the view
    PluginWrapper& wrapper() noexcept;

    // IPluginBase
    tresult PLUGIN_API initialize(FUnknown* context) override;
    tresult PLUGIN_API terminate() override;

    // IComponent
    tresult PLUGIN_API setActive(TBool state) override;
    tresult PLUGIN_API setState(IBStream* state) override;
    tresult PLUGIN_API getState(IBStream* state) override;

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(Vst::SpeakerArrangement* inputs, int32 numIns,
                                          Vst::SpeakerArrangement* outputs, int32 numOuts) override;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) override;
    tresult PLUGIN_API setProcessing(TBool state) override;
    uint32 PLUGIN_API getLatencySamples() override;
    uint32 PLUGIN_API getTailSamples() override;
    tresult PLUGIN_API process(Vst::ProcessData& data) noexcept override;

    // IEditController
    tresult PLUGIN_API setComponentState(IBStream* state) override;
    Vst::ParamValue PLUGIN_API getParamNormalized(Vst::ParamID id) override;
    tresult PLUGIN_API setParamNormalized(Vst::ParamID id, Vst::ParamValue value) override;
    IPlugView* PLUGIN_API createView(FIDString name) override;

    // IMidiMapping
    tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 channel,
                                                   Vst::CtrlNumber controller,
                                                   Vst::ParamID& id) override;

    // HostEditListener, message thread
    void beginParameterEdit(int index) noexcept override;
    void performParameterEdit(int index, float normalized) noexcept override;
    void endParameterEdit(int index) noexcept override;
    void beginParameterEditGroup() noexcept override;     // startGroupEdit()
    void endParameterEditGroup() noexcept override;       // finishGroupEdit()
    void latencyChanged() noexcept override;
    void parameterInfoChanged() noexcept override;

    // The definition names the parameter _iid: DEF_INTERFACE expands to code
    // that uses that name.
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override;
    REFCOUNT_METHODS(Vst::SingleComponentEffect)

private:
    void addBuses();
    void registerParameters();
    void registerShadowParameters();
    void notifyHostIfLatencyChanged();
    void applyParameterChanges(Vst::IParameterChanges& changes) noexcept;
    void collectEvents(Vst::IEventList& events) noexcept;
    void bindBuses(Vst::ProcessData& data) noexcept;
    void emitEvents(Vst::IEventList& events) noexcept;

    PluginWrapper pluginWrapper;          // PluginFormat::VST3
    int numMidiInputs = 0;                // min(layout.midiInputs.size(), 16), cached at construction
    bool active = false;
    bool prepared = false;                // a process() before the first activation renders silence
    std::atomic<int> lastReportedLatency {-1};   // -1 means the host has no baseline yet
};
```

Behaviour, call by call:

- **Constructor.**
  - Takes `OwningPointer<Plugin>` and builds `pluginWrapper(std::move(plugin),
    PluginFormat::VST3)`.
  - Calls `plugin().setHostEditListener(this)`.
  - Sets `processContextRequirements` to
    `needTransportState().needTempo().needTimeSignature().needProjectTimeMusic().needBarPositionMusic().needCycleMusic()`.
  - Walks `parameters()` for host ids in the shadow range when the layout has MIDI
    input; in debug it prints and asserts on each, as described under Decision. The
    check goes through `decodeShadowId`, so it fires only on an id that really is a
    registered shadow.
- **`initialize`.** Base first, then `adoptHostMessageThread()`, `addBuses()`,
  `registerParameters()`, `registerShadowParameters()`.
- **`addBuses`.**
  - Per audio bus: `addAudioInput`/`addAudioOutput(name, arrangementFor(numChannels),
    isMain ? kMain : kAux, isMain ? BusInfo::kDefaultActive : 0)`.
  - Per MIDI bus: `addEventInput`/`addEventOutput(name, 16)`.
- **`registerParameters`.** One `ProxyParameter` per `isHostExposed(i)` entry, in
  list order.
- **`registerShadowParameters`.** For each bus below `numMidiInputs`, each channel
  and each controller, it registers the shadow unless
  `parameters().indexOfHostId(id) >= 0`.
- **`terminate`.** Base only (it removes parameters and buses).
- **`setBusArrangements`.** Returns `kResultFalse`:
  - while `active`;
  - when the bus counts differ from `wrapper.busLayout()`;
  - or when `wrapper.setLayout(proposed)` refuses. `proposed` is the current layout
    with each audio bus's `numChannels = SpeakerArr::getChannelCount(...)`. The SDK
    buses then keep their arrangements, which is what `getBusArrangement` reports
    back.

  Otherwise it returns `SingleComponentEffect::setBusArrangements(...)`, which stores
  the arrangements on the SDK buses.
- **`canProcessSampleSize`.** `kResultTrue` for `kSample32` only.
- **`setActive(true)`.** `wrapper.prepare(int(std::lround(processSetup.sampleRate)),
  processSetup.maxSamplesPerBlock)`, then `wrapper.reset()`, then `prepared = active
  = true`. `setActive(false)` sets `active = false`. Both return the base's result.
- **`setProcessing(true)`.** `wrapper.reset()` (noexcept, legal on the audio thread).
  Always returns `kResultOk`.
- **`getLatencySamples`.** `latency = max(0, plugin().latencySamples())`, stores it
  in `lastReportedLatency`, returns the local.
- **`getTailSamples`.** 0 gives `kNoTail`; otherwise the value.
- **`notifyHostIfLatencyChanged`.** Message thread only. Off it, it posts itself with
  `callOnMessageThread`, capturing an `IPtr` to the adapter so the adapter outlives
  the call.

  The steps: `plugin().takeLatencyChanged()` (to clear the flag), then return when
  there is no `componentHandler`. Otherwise read `max(0, latencySamples())` and
  compare-exchange it against `lastReportedLatency`, doing nothing while the stored
  value is -1 or equal. The single caller that wins the exchange calls
  `componentHandler->restartComponent(Vst::kLatencyChanged)`.

  It is called from `setParamNormalized`, `setState`, `performParameterEdit` and
  `latencyChanged()`.
- **`parameterInfoChanged`.** `refreshInfo()` on every proxy, then
  `restartComponent(kParamTitlesChanged)`.
- **`getParamNormalized`.** `index = parameters().indexOfHostId(id)`. When `index >=
  0`, returns `wrapper.getNormalizedParameter(index)`; otherwise the base (shadow
  cache).
- **`setParamNormalized`.** When `index >= 0`: `wrapper.setNormalizedParameter(index,
  float(value))` (the wrapper drops it while held), then
  `notifyHostIfLatencyChanged()`, returning `kResultTrue`. Otherwise the base.
  Allocation-free on that path.
- **`getState`.**
  - A null stream returns `kInvalidArgument`, as for `setState`.
  - Calls `wrapper.saveState(stateContextOf(state))` inside `try`.
  - An empty document writes nothing and returns `kResultOk`.
  - Otherwise `writeAll`, returning `kResultFalse` if that fails.
  - Any exception returns `kResultFalse`. Nothing crosses the ABI.
- **`setState`.** `wrapper.loadState(readAll(*state), stateContextOf(state))` inside
  `try`, then `notifyHostIfLatencyChanged()`. Returns `kResultOk`, also when the
  load throws: the exception is swallowed. A null stream returns `kInvalidArgument`.
- **`setComponentState`.** Returns `kResultOk` and reads nothing.
- **`getMidiControllerAssignment`.** Returns `kResultFalse` when `busIndex >=
  numMidiInputs`, `channel ∉ [0, 15]`, `controller ∉ [0, 130]`, or the shadow id
  belongs to a plugin parameter. Otherwise `id = shadowIdFor(...)` and `kResultTrue`.
- **`createView`.** Returns null unless `name` is `Vst::ViewType::kEditor`. Otherwise
  `createPlugView(*static_cast<Vst::IAudioProcessor*>(this), plugin())`.
- **Edits.**
  - `beginParameterEdit(i)`: `wrapper.holdParameter(i)`, then `beginEdit(hostId)` if
    exposed.
  - `performParameterEdit(i, v)`: `performEdit(hostId, v)` if exposed, then
    `notifyHostIfLatencyChanged()`.
  - `endParameterEdit(i)`: `endEdit(hostId)` if exposed, then
    `wrapper.releaseParameter(i)`.
- **`queryInterface`.** `DEF_INTERFACE(Vst::IMidiMapping)`, then
  `SingleComponentEffect::queryInterface`.

**`process(ProcessData& data)`.** noexcept and allocation-free. The wrapper calls in
order:

1. If `!prepared`, clear every host output channel for `numSamples` and return
   `kResultOk`.
2. `wrapper.clearMidi()`.
3. `applyParameterChanges`. For each queue: when `indexOfHostId(id) >= 0`, read the
   last point and call `wrapper.setNormalizedParameter(index, value)`. Otherwise,
   when `decodeShadowId(id, numMidiInputs)` succeeds, call
   `wrapper.pushMidiIn(target.bus, shadowEvent(target, offset, value))` for every
   point. Anything else is ignored.
4. `wrapper.setPlayhead(data.processContext ? toPlayhead(*data.processContext) :
   Playhead {})`.
5. `collectEvents`. `fromVst3` each event, clamp `busIndex` to 0 when it is out of
   range (Live sends `INT_MIN`), then `wrapper.pushMidiIn(bus, event)`.
6. `wrapper.sortMidiInByOffset()`.
7. If `numSamples == 0` (a parameter flush), return `kResultOk`.
8. `bindBuses`.
   - For `bus < min(layout.inputs.size(), data.numInputs)`: `wrapper.bindInput(bus,
     inputs[bus].channelBuffers32, channels, numSamples)`, where `channels` is 0 when
     the table is null.
   - For each output bus up to `min(layout.outputs.size(), data.numOutputs)`:
     `wrapper.bindOutput(bus, outputs[bus].channelBuffers32, channels, numSamples,
     matchingInputTable, matchingInputChannels)`. The matching input is
     `data.inputs[bus]` when `bus < data.numInputs`.
9. `wrapper.process()`.
10. Set `silenceFlags = 0` on every output bus.
11. `emitEvents`, when `data.outputEvents` is set: for each `wrapper.midiOut()` bus
    and event, `toVst3`, then `outputEvents->addEvent`.

A `symbolicSampleSize` other than `kSample32` returns `kResultFalse` before step 1.

```cpp
// Factory.h
// A new factory holding one reference, listing every plugin in `module`, which
// must outlive it: each class's create context is a pointer to its description.
Steinberg::CPluginFactory* makeFactory(const ModuleDescription& module);
Steinberg::FUID classIdFor(const ModuleDescription& module, const PluginDescription& plugin);
```

- **`makeFactory`.** `new CPluginFactory(PFactoryInfo(vendor, url, email,
  Vst::kDefaultFactoryFlags))`. For each plugin it calls
  `registerClass(PClassInfo2(...), &createAdapter, &description)` with these fields:
  - `classIdFor(...)`
  - `kManyInstances`
  - `kVstAudioEffectClass`
  - the name
  - class flags 0
  - `subcategory.empty() ? defaultSubcategory(category) : subcategory`
  - the module vendor
  - the plugin version
  - `kVstVersionString`

  `registerClass` copies the info, so nothing static is needed, unlike Plug's.
- **`createAdapter(void* context)`.** `adoptHostMessageThread()`, then inside `try`:
  `auto plugin = description->create()`. A null plugin returns null. Otherwise it
  returns `static_cast<Vst::IAudioProcessor*>(new Adapter(std::move(plugin)))`. An
  exception returns null.
- `makeFactory` also calls `addHostContextCallback`. The SDK's callback type is
  `void (*)(FUnknown*)`, with no context pointer, so the module-wide pump is a
  function-local static `OwningPointer<HostRunLoop>` in `Factory.cpp`, replaced by
  `attachHostRunLoop(context)` on every call: a run loop, or null where the context
  offers none, which is everywhere but Linux.

```cpp
// EntryPoint.cpp, compiled into each module
extern "C" SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()
{
    static const auto module = MakeASound::describeModule();

    if (Steinberg::gPluginFactory == nullptr)
        Steinberg::gPluginFactory = MakeASound::VST3::makeFactory(module);
    else
        Steinberg::gPluginFactory->addRef();

    return Steinberg::gPluginFactory;
}
```

The SDK's `CPluginFactory` destructor clears `gPluginFactory`. The tests call
`makeFactory` directly and never touch the global. `makeFactory` returns
`CPluginFactory*`, which saves the cast.

```cpp
// PlugViewFactory.h
// A view on the plugin's editor (createEditor(), or GenericEditor when null),
// holding `owner` so the plugin outlives every view. Null where there is no UI.
Steinberg::IPlugView* createPlugView(FUnknown& owner, Plugin& plugin);

// PlugView.h, UI build only
class PlugView : public Steinberg::CPluginView,
                 public Steinberg::IPlugViewContentScaleSupport
{
public:
    PlugView(Steinberg::IPtr<FUnknown> owner, OwningPointer<Editor> editor);
    ~PlugView() override;

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override;
    tresult PLUGIN_API attached(void* parent, FIDString type) override;
    tresult PLUGIN_API getSize(Steinberg::ViewRect* size) override;
    tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override;
    tresult PLUGIN_API canResize() override;
    tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;
    tresult PLUGIN_API setFrame(Steinberg::IPlugFrame* frame) override;
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override;

    OBJ_METHODS(PlugView, Steinberg::CPluginView)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(Steinberg::CPluginView)
    REFCOUNT_METHODS(Steinberg::CPluginView)

private:
    void attachedToParent() override;
    void removedFromParent() override;

    Steinberg::IPtr<FUnknown> owner;
    OwningPointer<Editor> editor;
    OwningPointer<eacp::Graphics::EmbeddedView> embedded;
    OwningPointer<HostRunLoop> frameLoop;
    float scale = 1.f;
    bool sizedByHost = false;
};
```

The platform's half, `static nativeViewType()`, `rectScale()` and
`applyContentScale()`, is defined in `PlugView-macOS.cpp`, `PlugView-Windows.cpp`
and `PlugView-Linux.cpp`, one of which the target compiles.

- **`isPlatformTypeSupported`** is true for `nativeViewType()`:
  `kPlatformTypeNSView` on macOS, `kPlatformTypeHWND` on Windows and
  `kPlatformTypeX11EmbedWindowID` on Linux.
- **`attached`** refuses any other type, then calls the base, which runs
  `attachedToParent()`.
- **`attachedToParent`.**
  - `embedded = makeOwned<EmbeddedView>(systemWindow, {size.width, size.height})`,
    where size is `editor->initialSize()`.
  - `applyContentScale()`: `setPixelsPerPoint(scale)` off macOS, nothing on it.
  - `setContentView(editor->view())`.
  - `editor->onAttached()`.

  The embedded view fills the host's view by default, so the plugin never calls
  `setBounds`.
- **`removedFromParent`.** `editor->onRemoved()`, then `embedded.reset()`. The editor
  lives as long as the `PlugView`, so its `view()` stays stable.
- **Sizes.** `getSize` reports `editor->initialSize()` until the host sizes the view,
  then the last `onSize`. The `ViewRect` is in points on macOS and in pixels (points
  × `scale`) on Windows and Linux. `onSize` stores the rect and calls
  `embedded->setSize(width / scale, height / scale)`.
- **`canResize`** is `editor->isResizable()`. **`checkSizeConstraint`** returns
  `kResultTrue` when resizable and the base otherwise. Real size policy is stage 6.
- **`setContentScaleFactor`.** Stores `scale` and `applyContentScale()`s it, which
  on macOS is nothing: `rectScale()` is 1 there, and `scale` elsewhere.
- **`setFrame`.** Base, then `frameLoop = attachHostRunLoop(frame)`: on Linux a pump
  on the frame's `IRunLoop` when it answers one, null on a null frame and everywhere
  else.
- Keys are left at the base's `kResultFalse`: eacp receives its own key events in its
  NSView or HWND.

```cpp
// HostRunLoop.h
class HostRunLoop
{
public:
    virtual ~HostRunLoop() = default;
};

// Null when `context` offers no run loop. HostRunLoop-Linux.cpp returns an
// IEventHandler registered with the IRunLoop for messageLoopFd(), calling
// pumpMessageLoop() when it is readable and unregistering in its destructor;
// addRef/release are no-ops, C++ owns it. HostRunLoop-Default.cpp returns null.
OwningPointer<HostRunLoop> attachHostRunLoop(FUnknown* context);
```

### Build

`makeasound_add_plugin` gains a `VERSION` argument (default `1.0.0`) and the `VST3`
format; the signature is otherwise unchanged. For `VST3`:

```cmake
if (NOT TARGET MakeASoundVST3)
    message(STATUS "${name}: VST3 format skipped (MakeASoundVST3 not built)")
    continue()
endif ()

get_target_property(vst3_entry MakeASoundVST3 MAKEASOUND_VST3_ENTRY)
get_target_property(vst3_main vst3sdk MAKEASOUND_VST3_SDK_MAIN)

set(target ${name}-VST3)
add_library(${target} MODULE "${vst3_entry}" "${vst3_main}")
target_link_libraries(${target} PRIVATE MakeASoundVST3 ${name})   # format first, as Standalone
set(bundle_dir "${CMAKE_BINARY_DIR}/VST3")                        # every bundle in one folder
```

- **Common to all platforms.**
  - `FOLDER "${ARG_FOLDER}"`, `OUTPUT_NAME "${ARG_OUTPUT_NAME}"`, `PREFIX ""`.
  - The output directories are wrapped in `$<1:...>` so multi-config generators
    append no per-config folder.
  - `ARCHIVE_OUTPUT_DIRECTORY` and `PDB_OUTPUT_DIRECTORY` are
    `${CMAKE_CURRENT_BINARY_DIR}`, keeping `.lib`/`.exp`/`.pdb` out of the bundle.
  - The bundle path is recorded on the target as `MAKEASOUND_VST3_BUNDLE`.
- **macOS.**
  - `BUNDLE TRUE`, `BUNDLE_EXTENSION vst3`, `LIBRARY_OUTPUT_DIRECTORY
    $<1:${bundle_dir}>`.
  - `MACOSX_BUNDLE_INFO_PLIST` from `MAKEASOUND_VST3_PLIST`.
  - `MACOSX_BUNDLE_BUNDLE_NAME "${ARG_OUTPUT_NAME}"`, `MACOSX_BUNDLE_GUI_IDENTIFIER
    "${ARG_BUNDLE_ID}.vst3"`.
  - `MACOSX_BUNDLE_BUNDLE_VERSION` and `MACOSX_BUNDLE_SHORT_VERSION_STRING` from
    `${ARG_VERSION}`; `MACOSX_BUNDLE_COPYRIGHT "${ARG_COMPANY}"`.
  - The sealed exports are `target_link_options(PRIVATE
    "LINKER:-exported_symbols_list,<vst3sdk's MAKEASOUND_VST3_SDK_EXPORTS>")` plus
    `LINK_DEPENDS` on that file. The SDK's `macexport.exp` already lists exactly
    `_GetPluginFactory`, `_bundleEntry` and `_bundleExit`, so there is no export list
    of ours.
  - `POST_BUILD`: `cmake -E copy <PkgInfo>
    "$<TARGET_BUNDLE_CONTENT_DIR:tgt>/PkgInfo"`, then `codesign --force --sign -
    "$<TARGET_BUNDLE_DIR:tgt>"`. The result is
    `VST3/<Out>.vst3/Contents/{Info.plist,PkgInfo,MacOS/<Out>,_CodeSignature}`.
- **Windows.**
  - `SUFFIX .vst3`.
  - `LIBRARY_OUTPUT_DIRECTORY $<1:${bundle_dir}/<Out>.vst3/Contents/<arch>-win>`,
    where `<arch>` is from `CMAKE_CXX_COMPILER_ARCHITECTURE_ID`: `x64` gives
    `x86_64`, `ARM64` gives `arm64`, `X86` gives `x86`.
  - The result is `VST3/<Out>.vst3/Contents/x86_64-win/<Out>.vst3`. Only
    `SMTG_EXPORT_SYMBOL` functions are exported from a DLL, so nothing else is
    needed.
- **Linux.**
  - `SUFFIX .so`.
  - `LIBRARY_OUTPUT_DIRECTORY $<1:${bundle_dir}/<Out>.vst3/Contents/<arch>-linux>`,
    where `<arch>` is `CMAKE_SYSTEM_PROCESSOR` (`x86_64`, `aarch64`).
  - `LINKER:--version-script=<MAKEASOUND_VST3_LINUX_EXPORTS>` plus `LINK_DEPENDS`.
    With `local: *`, the module's own copies of MakeASound, eacp and Miro bind
    internally, so nothing binds to a host's or a sibling plugin's copy (Plug's
    sealed-exports rationale, applied to ELF).
  - The result is `VST3/<Out>.vst3/Contents/x86_64-linux/<Out>.so`.
- **Install.** With `MAKEASOUND_INSTALL_PLUGINS` on, a final `POST_BUILD` (after the
  codesign) runs `cmake -D SOURCE=<bundle> -D DEST_DIR=<folder> -P
  CMake/InstallPluginBundle.cmake` (path from `CMAKE_CURRENT_FUNCTION_LIST_DIR`).
  - The folder is `~/Library/Audio/Plug-Ins/VST3` on macOS,
    `$ENV{LOCALAPPDATA}/Programs/Common/VST3` on Windows (the per-user location,
    writable without admin), and `~/.vst3` on Linux.
  - The script makes the folder, removes the old bundle and copies the directory. A
    failure is a `WARNING`, never an error.
  - Stage 5 reuses the option and the script for `.component`.
- **`PluginValidator`.** pluginval is a runnable target, not a build step, so
  CLion, CI and a unit test all drive the same code. The logic is the library
  `MakeASoundPluginval` (namespace `MakeASound::Pluginval`, eacp only):

  ```cpp
  struct Options
  {
      int strictness = 10;
      bool guiTests = true;
      eacp::Time::MS timeout {120000};
      std::string version = "v1.0.4";
      eacp::FilePath directory = eacp::OnlineResource::defaultDirectory();
  };

  eacp::FilePath fetch(const Options& options = {});
  Result validate(const eacp::FilePath& pluginval, const eacp::FilePath& bundle,
                  const Options& options = {});
  Vector<eacp::FilePath> findBundles(const eacp::FilePath& directory);
  ```

  1. `fetch` asks `eacp::OnlineResource::fetch` for
     `https://github.com/Tracktion/pluginval/releases/download/<version>/<asset>`
     with `Freshness::trust` and `version` as the sidecar's version, so a second
     run downloads nothing and a new tag downloads once. The zip is unpacked by
     eacp; a failure throws `std::runtime_error`.
  2. The asset (`pluginval_macOS.zip`, `pluginval_Windows.zip`,
     `pluginval_Linux.zip`), the binary inside it
     (`pluginval.app/Contents/MacOS/pluginval`, `pluginval.exe`, `pluginval`) and
     the chmod are one TU per platform. eacp's zip reader drops the mode bits, so
     macOS and Linux add the execute bits; Windows throws on an ARM64 machine,
     since Tracktion publishes no arm64 build.
  3. `validate` runs `--strictness-level N --timeout-ms N --verbose
     [--skip-gui-tests] --validate <bundle>` through `eacp::Processes::run`,
     times it, and passes on exit code 0, with stdout and stderr as the log.

  The executable takes `[--strictness N] [--skip-gui-tests] [--timeout-ms N]
  [--version vX.Y.Z] [--logs <dir>] <bundle or folder>...`: each `.vst3` named,
  and every `.vst3` in each folder named. It prints `PASS`/`FAIL` per bundle with
  the last 25 non-blank log lines of a failure, writes each log to `<logs>/<bundle>.log`
  (the system temp folder's `PluginValidator/` unless `--logs` says where) and
  exits with the number that failed, or 1 with the usage when given nothing. It
  depends on no plugin target and knows nothing of this build: what to validate is
  always an argument, so it is one tool for any bundle from any tree.

  **Locally:** `./build/Tools/PluginValidator/PluginValidator build/VST3`, or run
  the target from CLion with that argument. pluginval is not installed on this Mac
  and need not be.

### Tests

`Tests/CMakeLists.txt`: in the `MAKEASOUND_BUILD_PLUGIN` branch, `if (TARGET
MakeASoundVST3)` adds `VST3Tests.cpp` and links `MakeASoundVST3` and
`vst3sdk-hosting`. In the Apple/Linux allocation branch, the same condition adds
`VST3AllocationTests.cpp`. Should the link ever ask for `moduleHandle` (only if
something pulls `moduleinit.o`), define `void* moduleHandle = nullptr;` with C++
linkage in `VST3Tests.cpp`, as Plug's `HostEntry.cpp` does.

**`Tests/VST3TestHost.h`:**
- `TestComponentHandler` implements `IComponentHandler` and `IComponentHandler2`. It
  records begin/perform/end per id, restart flags, and group starts and ends. Its
  `addRef`/`release` are no-ops; it is stack-owned.
- `TestAttributeStream` is a `MemoryStream` that also answers `IStreamAttributes`,
  with a fixed `kStateType`.
- `AdapterHost<P>` builds `Steinberg::owned(new VST3::Adapter(makeOwned<P>()))`, then
  `initialize(nullptr)`, `setComponentHandler(&handler)`,
  `setupProcessing({kRealtime, kSample32, 64, 48000})`, `setActive(true)`,
  `setProcessing(true)`. It owns host buffers per bus, a `Vst::ParameterChanges`,
  input and output `Vst::EventList`s (capacity 64), a `Vst::ProcessContext` and a
  `Vst::ProcessData`, with `fillBlock(n)` and `run()`. The destructor tears down in
  reverse.
- The plugins come from `Tests/TestPlugins.h`, unchanged: `GainPlugin` (effect, with
  a `ChoiceParam`, a bypass `BoolParam`, an `HzParam` and a session-only parameter)
  and `SynthPlugin` (instrument, MIDI out echo).
- `VST3Tests.cpp` adds a `LatencyPlugin`: a `StatePlugin` whose `latencySamples()` is
  a parameter's value, with a method that calls `notifyHostLatencyChanged()`.

**`Tests/VST3Tests.cpp`, about 22 cases under `VST3/`:**

- **The factory.**
  - `makeFactory` over a module of the two test plugins counts 2.
  - `getFactoryInfo` gives the vendor.
  - `getClassInfo2`: category `kVstAudioEffectClass`, subcategories `Fx` and
    `Instrument|Synth`, class flags 0, cid `classIdFor`, version.
  - `createInstance` with the cid and `IComponent::iid` gives an object that also
    answers `IAudioProcessor`, `IEditController` and `IMidiMapping`.
  - An unknown cid gives `kNoInterface`.
- **Buses.**
  - `getBusCount`/`getBusInfo`: the effect has one stereo main bus each way; the
    instrument has no audio in, one stereo out, one event in and one event out, each
    `kDefaultActive`.
  - `setBusArrangements`: mono/mono is accepted and reflected in
    `wrapper().busLayout()`; mono-in/stereo-out is refused and `getBusArrangement` is
    unchanged; any call while active is refused.
  - `canProcessSampleSize(kSample64)` is false.
- **A whole block.** The effect, after `setActive`:
  - The plugin saw `spec.sampleRate == 48000`, `maxBlockSize == 64` and `format() ==
    VST3`.
  - At 0 dB the output equals the input, both with separate buffers and with in-place
    (aliased) ones.
  - A playhead with tempo and time-signature flags arrives converted.
  - A host `process` before the first `setActive` writes silence and the plugin's
    `processed` stays 0.
- **Parameter info.**
  - `getParameterCount` equals the exposed count, plus 2,096 for the instrument.
  - Each `getParameterInfo(i)` has `id == entry.hostId`, title equal to the display
    name, units equal to the label, `stepCount == numSteps()`, and the default.
  - The choice parameter carries `kIsList`, the bypass parameter `kIsBypass`, and
    every one `kCanAutomate`.
  - Every shadow parameter is `kIsHidden` with an id at or above `0x7fff0000`.
- **Normalized set and get.**
  - `setParamNormalized(hostId, 0.25)` moves the plugin's parameter, and
    `getParamNormalized` reads 0.25.
  - Setting the parameter directly on the plugin is visible to `getParamNormalized`.
  - `getParamStringByValue` equals `valueToText(toPlain(v))`, and
    `getParamValueByString` inverts it.
  - `normalizedParamToPlain` on `HzParam` equals the parameter's own skewed
    `toPlain`.
- **The automation queue.** Two points on the gain id leave the plugin at the last
  one. A zero-sample flush applies parameters and does not call the plugin's
  `process`.
- **The edit gate** (Plug's five cases, on host ids):
  - A hold drops both the queue and `setParamNormalized`.
  - Release accepts again.
  - Nested holds release at the outermost end.
  - An unmatched end cannot underflow.
  - The handler sees begin, perform and end with the host id, and nothing for a
    parameter that is not exposed.
  - An edit group reaches `IComponentHandler2`.
- **State.**
  - Moving every parameter, then `getState` into a `MemoryStream`, then `setState` on
    a fresh adapter restores each one, the session-only parameter included.
  - Through a `TestAttributeStream` with `kStateType = "Default"`, the session-only
    parameter is absent on save and left alone on load.
  - `getState` on a `std::thread` returns the snapshot without the message thread
    being pumped.
  - `setState` on a `std::thread` lands the parameters at once, and the rest after a
    `pumpUntil`.
  - The controller's state calls return `kNotImplemented`. Through an
    `IEditController*` they are spelled `setEditorState`/`getEditorState`: once
    `VST3Common.h` is included first, the rename applies to `IEditController` itself.
  - `setComponentState` returns `kResultOk` and changes nothing.
- **MIDI in and out.**
  - On the instrument, a note-on at offset 5 and a note-off at 10, one with `busIndex
    = INT_MIN`, reach the plugin in order on bus 0.
  - The echo comes back as `kNoteOnEvent`/`kNoteOffEvent` with the same offsets.
  - A SysEx `kDataEvent` round trips.
- **CC through mapping.**
  - `getMidiControllerAssignment(0, 3, 74, id)` gives `kResultTrue` and `id ==
    shadowIdFor(0, 3, 74)`.
  - Queue points at offsets 0 and 32 give two `ControlChange` events on channel 3,
    controller 74, with those values.
  - The echo comes back as `kLegacyMIDICCOutEvent` with controller 74 and `value ==
    lround(v * 127)`.
  - Pitch bend gives `value * 2 - 1`, and goes back out as 14 bits across `value` and
    `value2`.
  - Aftertouch gives `ChannelAftertouch`.
- **Program change.** The `kCtrlProgramChange` mapping gives `ProgramChange` with
  `lround(v * 127)`, echoed as `kLegacyMIDICCOutEvent` with `kCtrlProgramChange`.
- **Mapping limits.**
  - `kResultFalse` for bus 1, channel 16, controller 131 and any effect plugin.
  - All 2,096 shadow ids are distinct and none equals a plugin host id.
  - `decodeShadowId` round trips `shadowIdFor`.
- **Latency.**
  - With no baseline, `setParamNormalized` on the latency parameter restarts nothing.
  - After `getLatencySamples()`, moving it gives one `kLatencyChanged`; setting the
    same value again gives none.
  - `notifyHostLatencyChanged()` through the listener gives one restart.
  - A `setState` that moves the latency gives one restart.
  - `getTailSamples` is `kNoTail` for 0.
- **Parameter info changed.** `notifyHostParameterInfoChanged()` gives
  `restartComponent(kParamTitlesChanged)`.
- **The view** (`#if MAKEASOUND_VST3_HAS_VIEW`).
  - `createView("editor")` is non-null.
  - `isPlatformTypeSupported` is true for the native type and false for another.
  - `getSize` equals `GenericEditor`'s initial size.
  - `canResize` is true.
  - The view is released without attaching: attaching needs a native window, which
    pluginval covers.
  - `createView("other")` is null.

**`Tests/VST3AllocationTests.cpp`** (Apple and Linux, with `Probe::allocationsIn`
from `AllocationProbe.h`), after Plug's `Vst3AdapterAllocationTests`:
- **A whole `process` call** for the effect, in place and not, and for the
  instrument. The block carries two points per exposed parameter, note-on and
  note-off events, CC 1 and pitch-bend shadow queues, a process context, and an
  echoed output into a preallocated `EventList`. It is measured over block sizes 64,
  17, 1, 63, then a zero-sample flush, and again after
  `setActive(false)`/`setupProcessing(44100, 32)`/`setActive(true)`. Queues and
  events are filled outside the measured lambda.
- **`setParamNormalized`** on every exposed id, and on a shadow id.
- **The held path**: `beginParameterEdit` for all, then a block, then
  `setParamNormalized` for all, then `endParameterEdit` for all.

- **`Pluginval/exampleBundlesPassAtStrictness10`** (`PluginvalTests.cpp`, when
  `MakeASoundPluginval` and `MakeASoundVST3` are targets): the library over
  `findBundles(MAKEASOUND_VST3_DIR)`, every result passed. It needs the network
  once and ~20 s a bundle, so it runs only when `MAKEASOUND_PLUGINVAL` is set
  (`nogui` skips the editor tests) and otherwise returns and passes; NanoTest has
  no skip.

### CI and pluginval

`.github/workflows/ci.yml`, `build` job:
- **Building.** Every desktop entry builds the `-VST3` bundles in the existing Debug
  and Release loop, because `MAKEASOUND_BUILD_EXAMPLES` is on, and runs the new
  suites under `ctest`.
- **Matrix flag.** Add a `pluginval: true` key on macOS Universal, Windows MSVC
  (x64), Windows Clang (x64) and Linux GCC. Not on Windows ARM64, where there is no
  pluginval binary, and not on Linux Clang, which is redundant.
- **New step `pluginval (Release)`**, `if: matrix.pluginval`: runs
  `build-Release/Tools/PluginValidator/PluginValidator --logs
  build-Release/pluginval/logs build-Release/VST3`, both built by the default
  target.
- **Linux runs headless.**
  - The audio-packages step also installs `xvfb`, and `libcurl4-openssl-dev`, which
    `eacp-network` links.
  - The step runs `xvfb-run -a ... --skip-gui-tests`, because JUCE's Linux host
    wants an X server even with GUI tests skipped, and the eacp GPU editor has no
    Vulkan device on the runner.
- **Logs.** A failure uploads `build-Release/pluginval/logs/` with
  `actions/upload-artifact@v4`.
- **The iOS job** needs no change: `MakeASoundVST3` and every `-VST3` target skip
  with a status line.
- **If the sweep runs long.** Two bundles at strictness 10 should take a few minutes
  per job. If the sweep regularly passes 15 minutes, move it into a nightly
  `pluginval.yml`, as Plug did, rather than lowering the strictness.

### Fallout in this repository

| place | change |
| --- | --- |
| `Plugin/Realtime/MessageThread.{h,cpp}` | three additive functions; still the core's only eacp TU |
| `Plugin/CMakeLists.txt` | `add_subdirectory(VST3)` after `Standalone` |
| `CMake/MakeASoundPlugin.cmake` | `VST3` format, `VERSION` argument (also applied to the Standalone's `MACOSX_BUNDLE_BUNDLE_VERSION`/`SHORT_VERSION_STRING`), header comment lists `<Name>-VST3` |
| `CMakeLists.txt` | `MAKEASOUND_INSTALL_PLUGINS` option, `add_subdirectory(Tools)` |
| `Plugin/Validation/`, `Tools/PluginValidator/` | `MakeASoundPluginval` and the `PluginValidator` executable, which depends on no plugin target |
| `Plugins/Gain`, `Plugins/Synth` | `FORMATS Standalone VST3`, which adds `Gain-VST3` (`MakeASound Gain.vst3`) and `SynthPlugin-VST3` (`MakeASound Synth.vst3`, id `com.makeasound.synth.vst3`) |
| `ThirdParty/` | `hosting/parameterchanges`, `hosting/eventlist`, target `vst3sdk-hosting`, README selection and update note |
| `Tests/CMakeLists.txt`, new test files | as above |
| `.github/workflows/ci.yml` | pluginval step running `PluginValidator`, `xvfb` and libcurl on Linux, log upload |
| `CLAUDE.md`, `README.md` | the format: target, files, threading, the shadow scheme, the bundle layout, `PluginValidator`, the install option |
| `plan.md` | stage 4 marked landed, a "Landed:" paragraph and divergences in this section's style |

No existing public API changes. `MakeASoundPlugin` gains three free functions and its
headers stay SDK-free and eacp-free. `makeasound_add_plugin` keeps its signature and
gains one optional argument.

### Fallout downstream

None. Plug and tamber-web leave `MAKEASOUND_BUILD_PLUGIN` off, so `ThirdParty`,
`MakeASoundVST3` and the new options never configure for them. The device library is
untouched.

### Done when

- `PluginValidator build/VST3` passes at strictness 10 on `MakeASound Gain.vst3`
  and `MakeASound Synth.vst3` on this Mac and exits 0, downloading pluginval on
  the first run only; `MAKEASOUND_PLUGINVAL=1` runs the same through
  the test suite.
- CI's pluginval step is green on macOS universal, Windows MSVC x64, Windows clang-cl
  x64 and Linux GCC (headless, GUI tests skipped). Every desktop job builds both
  bundles, Windows ARM64 included.
- `VST3/` and the VST3 allocation cases are green on macOS and Linux, and `VST3/` on
  Windows.
- On macOS, both bundles carry `Contents/PkgInfo` (`BNDL????`), a `BNDL` plist with
  `<BUNDLE_ID>.vst3`, a valid ad-hoc signature (`codesign -v`), and `nm -gU` shows
  exactly the three exports. On Linux, `nm -D --defined-only` shows exactly three.
- Each bundle loads in a real DAW on the Mac, opened by hand after a build (the
  default `MAKEASOUND_INSTALL_PLUGINS` copies it into the plug-in folder):
  - the generic editor opens, resizes and drives automation;
  - automation plays back;
  - the synth plays from a MIDI track and answers the mod wheel and CC 123;
  - a saved project reopens with its values.
- CLAUDE.md, README and this plan describe the format as it is.

## Stage 5: the AU format

The second DAW format, macOS only: an Audio Unit v2 `.component` over the same
`PluginWrapper`, built on Apple's AudioUnitSDK. The proof is `auval -strict` on both
example bundles, on this Mac and in the macOS CI job, after an install step.

Landed: `MakeASoundAUDescribe` and `MakeASoundAU` in `Plugin/AU/`, built when
`TARGET ausdk`, which `ThirdParty/CMakeLists.txt` defines on `APPLE AND NOT IOS`
from Apple's AudioUnitSDK through CPM; `makeasound_add_plugin` gains the `AU`
format (`<Name>-AU`, `<build>/AU/<OUTPUT_NAME>.component`,
identifier `<BUNDLE_ID>.component`, `PkgInfo`, ad-hoc signature, the
`Components` folder under `MAKEASOUND_INSTALL_PLUGINS`), and `Plugins/Gain` and
`Plugins/Synth` build `FORMATS Standalone VST3 AU`. `PluginValidator build/AU`
passes `auval -strict` on `MakeASound Gain.component` and `MakeASound
Synth.component` on this Mac; the macOS CI job gains the `auval (Release)` step.
`MakeASoundPluginval` validates a `.component` through auval, `PluginValidator`
takes `--stress N` and fetches pluginval only when a `.vst3` is named, and the
env-gated test sweeps `MAKEASOUND_AU_DIR` as well. Tests: `AU/` in `AUTests.cpp`
over `Tests/AUTestHost.h` and `AUAllocationTests.cpp`, with the echo instrument
as a second plugin of `PluginTests.cpp`'s module. README and CLAUDE.md describe
the format. Where it differs from the section below:

- **Reconciliation keeps two baselines**, `lastHostValue` and `lastPluginValue`
  per exposed entry, not one. A host value that moved, on a parameter no gesture
  holds, goes to the plugin and both baselines take the result; otherwise
  `Globals()` is written back only when the plugin moved on its own or a held
  parameter's host write has to be undone. Mirroring the plugin's value back on
  every block, as the section had it, overwrote the host's value with the
  plugin's snapped form of it: auval's "Parameter did not retain set value when
  Initialized" sets off-grid values on a stepped parameter and reads them back.
  `Initialize` pushes `Globals()` into the plugin before seeding both baselines,
  so a value the host set before initializing reaches the plugin.
- **`SetParameter` is overridden** to refuse a global id that is not an exposed
  host id (`kAudioUnitErr_InvalidParameter`). With `Globals()` in map mode the
  SDK would otherwise insert any id a host or auval writes.
- **A host `SetParameter` writes the wrapper at once**, not only at the next
  render, unless a gesture holds the parameter, and `SaveState` does not copy
  `Globals()` into the plugin: a save between a host write and the render it
  lands in would otherwise have to choose between the host's value and one the
  plugin moved itself, and copying undid the plugin's own moves. On the message
  thread `SetParameter` also checks the latency, as VST3's `setParamNormalized`
  does. `RestoreState` with no document of ours pushes the restored `Globals()`
  into the plugin.
- **The factory refuses a foreign manufacturer**, and an unknown subtype, before
  dispatching, returning null; the constructor still throws for a subtype it
  cannot create, should the factory be bypassed.
- **The latency `PropertyChanged` is posted on the calling thread**, not
  marshalled to the message thread: the compare-exchange already makes only one
  caller post, and `takeLatencyChanged` is consumed only when that caller is the
  message thread. `performParameterEdit` and `RestoreState` check it too.
- **The adapter property is `MakeASound::AU::adapterProperty`** (64000), a
  namespace constant in `CocoaUI.h`, not a `kMakeASoundAUProperty_Adapter` macro.
- **`MAKEASOUND_AU_PKGINFO`** is a property of `MakeASoundAU` that always points
  at `../VST3/PkgInfo`, the same file, rather than reading `MakeASoundVST3`'s
  property, so the AU format does not need the VST3 target to exist.
- **`ausdk` is always a unity build**, as `vst3sdk` is: nothing in it is edited.
  It is pinned to `GIT_TAG AudioUnitSDK-1.4.0`.
- **`ValuesHaveStrings` is set only for a stepped choice or bool**, the
  parameters that answer `GetParameterValueStrings`; every one still answers
  `ParameterStringFromValue`, and `ParameterValueFromString` hands back the
  current value when `textToValue` yields a non-finite number, never NaN.
- **MIDI in is staged.** `MusicDeviceMIDIEvent`/`MusicDeviceSysEx` may arrive on
  a host thread other than the render thread, so the `Handle*` overrides push
  into a staging `MIDI::Buffer` of the adapter's (1024 events, reserved in
  `PostConstructor`, dropping when full) under a `SpinLock`, as JUCE's wrapper
  does, and `Render` starts with `clearMidi()` and moves the staged events onto
  MIDI-in bus 0 under the same lock; there is no `clearMidi()` at the end. AU
  delivers one MIDI stream, so only bus 0 is ever fed. The velocity-0 branch in
  `HandleNoteOn` is gone: `AUMIDIBase::HandleMIDIEvent` already turns one into
  `HandleNoteOff`.
- **Outputs follow `DoRenderBus`.** With one output element bus 0 is bound from
  its buffer list, which the SDK pointed at the host's buffers or prepared; with
  more, every bus, bus 0 included, is `PrepareBuffer(frames)`, because the SDK
  renders them all into its own buffers and copies each out when its bus is
  rendered, so a host rendering bus 1 first still gets bus 0.
- **The MIDI output callback goes through `RealtimeSwap`**, published by
  `SetProperty` and taken by `currentForBlock()` once per render, instead of a
  flag over a plain struct that a render could read half rewritten.
- **The playhead's sample time is the transport's** `sampleInTimeline`, as VST3
  reports `projectTimeSamples`; the timestamp's `mSampleTime` only when
  `CallHostTransportState` does not answer.
- **Nothing releases the message thread.** The `__attribute__((destructor))`
  function in `EntryPoint.cpp` is gone: dyld registers one when the image loads,
  so it runs after the image's function-local statics are destroyed, and once
  `RealtimeSwap`'s reclaimer had started eacp's `callAfter` scheduler, auval
  aborted at exit locking the scheduler's destroyed mutex. On macOS releasing
  only stops that scheduler, which its own static destructor already does.
- **`kAudioUnitProperty_ClassInfoFromDocument`** is answered (writable,
  `sizeof(CFPropertyListRef)`) and routed to `RestoreState`, since the SDK does
  not dispatch it and Logic restores a document through it. Both paths load a
  `Session` document.
- **`kAudioUnitProperty_BypassEffect` is not implemented.** auval warns about it
  as a recommended property and passes; mapping it onto a `{.bypass = true}`
  parameter, as the VST3 adapter's `kIsBypass` flag does, is left for stage 6.
- **`HandleNonNoteEvent` also carries the program change**, which the SDK's
  `HandleProgramChange` would deliver with no frame, and SysEx longer than
  `MIDI::SysEx::maxBytes` (32) is dropped, as in every format.
- **The module sets `OBJCXX_STANDARD 23`**: the view TU is Objective-C++ and
  includes the SDK's `<expected>` too. `ausdk`'s feature is PUBLIC, so the
  tests compile the entry and view TUs into a test-only `MakeASoundAUTestModule`
  that links `MakeASoundAU` PRIVATE; `MakeASoundTests` stays C++20, includes no
  adapter header and hosts the unit through the C API.
- **The `.component` is laid out by hand**, not as a CMake `BUNDLE` target: CMake
  rewrites a bundle target's `Info.plist` on every configure, which replaced the
  generated one until the next relink. The module lands in `Contents/MacOS` and
  the post-build writes the plist, `PkgInfo` and the signature, the way the
  Windows and Linux VST3 layouts are already made.
- **One plist tool, not one per plugin.** The module exports a second C function,
  `MakeASoundAUWritePlist`, beside the factory; it calls
  `writeAudioComponentsPlist` (`Plist.{h,cpp}` in `MakeASoundAUDescribe`) with the
  module's own `describeModule()`. A single executable, `MakeASoundAUPlistGen`,
  built with `MakeASoundAU`, `dlopen`s the module the post-build hands it and
  calls that function. The per-plugin `<Name>-AUPlistGen` and the
  `MAKEASOUND_AU_PLIST_GEN` property are gone: no generator target per plugin, no
  second link of each core into an executable, and the plist comes from the very
  binary it describes.
- **auval's verdict** is exit code 0 with `AU VALIDATION SUCCEEDED` in the output
  (not `PASS`), and the components are read with
  `CFBundleCopyInfoDictionaryForURL`, not through a `CFBundle`, which is cached by
  path and showed a rebuilt bundle's old plist.

### Decision

There is one class per module, `MakeASound::AU::Adapter`, deriving from
`ausdk::MusicDeviceBase` (which is `AUMIDIBase` over `AUBase`, so one class answers
the effect, music-effect, instrument and MIDI-processor selectors alike) plus
`HostEditListener`. It owns a `PluginWrapper` built with `PluginFormat::AU` and
drives it in the documented order, never calling the plugin's `process`. The design
is Plug's AU adapter mapped onto MakeASound's types and corrected where MakeASound's
VST3 adapter already departed from Plug:

- **One exported factory function per module**, `MakeASoundAUFactory`, not Plug's
  four. It reads `desc->componentType` and dispatches to
  `ausdk::AUBaseFactory`, `ausdk::AUMIDIEffectFactory` or
  `ausdk::AUMusicDeviceFactory` over `Adapter`, so the right dispatch table serves
  each type. The instance finds its `PluginDescription` through
  `AudioComponentGetDescription` on its own component: `describeModule().plugins`
  keyed by `pluginCode` (the subtype), so a module with several plugins ships them in
  one bundle, as VST3 does.
- **Component type by category** (`ComponentType.{h,cpp}`, SDK-free):
  `Category::Instrument` → `aumu`; `Category::MidiEffect` → `aumi`; `Effect` with a
  MIDI input bus → `aumf`; otherwise `aufx`. Subtype is `pluginCode`, manufacturer
  is `manufacturerCode`. It constructs the plugin to read the layout, in the
  generator and at runtime both, so a plugin constructor must be safe in a CLI.
- **`AudioUnitParameterID` is the parameter's 31-bit host id**
  (`ParameterList::Entry::hostId`), looked up with `indexOfHostId`, never an index:
  an inserted parameter keeps everyone else's automation. `Globals()` therefore runs
  in map mode, and every id is seeded in `PostConstructor` so no `SetParameterRT`
  ever inserts on the render thread. `GetParameterList` is overridden to return the
  exposed ids in declaration order, not map order.
- **Only `isHostExposed` entries are listed**; `GetParameterInfo` on another id is
  `kAudioUnitErr_InvalidParameter`. `GetParameterValueStrings` answers for a
  `ChoiceParam` (and a `BoolParam`), `kAudioUnitProperty_ParameterStringFromValue`
  and `ParameterValueFromString` go through `valueToText`/`textToValue`, so a
  skewed parameter reads right in the host. Units: `Indexed` for a stepped parameter,
  `Boolean` for a bool, `Generic` otherwise; flags readable, writable,
  `HasCFNameString`, `CFNameRelease`, and `ValuesHaveStrings` where they do.
- **Reconciliation.** `CanScheduleParameters()` is false (true makes the SDK
  `push_back` on the render thread), so every host write lands in `Globals()` at
  once and the block reads it: at the top of each render, for each entry, the
  host's `GetParameterRT` is applied through `setParameter` when it moved since the
  last block and no gesture holds it; otherwise the plugin's value is mirrored back
  with `SetParameterRT`. The baseline is a per-entry `lastHostValue`.
- **Gestures** leave through `AUEventListenerNotify`:
  `kAudioUnitEvent_BeginParameterChangeGesture`, `ParameterValueChange` (after
  `Globals()->SetParameter` with the plain value), `EndParameterChangeGesture`;
  begin also `holdParameter`s and end `releaseParameter`s. `beginParameterEditGroup`/
  `endParameterEditGroup` are no-ops. `parameterInfoChanged` posts
  `PropertyChanged(kAudioUnitProperty_ParameterList)` and `ParameterInfo`.
- **Latency and tail.** `GetLatency()` is `latencySamples()` over the output rate
  and records the figure; a change reaches `PropertyChanged(kAudioUnitProperty_Latency)`
  through the same compare-exchange on the last fetched figure the VST3 adapter
  uses, on the message thread. `SupportsTail()` is true and `GetTailTime()` is
  `tailSamples()` over the rate.
- **Buses and formats.** `PostConstructor` sets every element to float32
  non-interleaved at the declared channel count and `SetWillAllocateBuffer(true)` on
  the outputs. `SupportedNumChannels` publishes the pairs `acceptsLayout` accepts
  for main-bus counts 1..8 on each side (0 for a side with no bus), from a member
  vector that outlives the call. `ValidFormat` judges the element alone, since
  hosts set one at a time: its count must appear on that side of some published
  pair. `Initialize()` builds the negotiated layout from each element's
  `NumberChannels()`, runs it through `PluginWrapper::setLayout`
  (`kAudioUnitErr_FormatNotSupported` when refused), prepares at
  `Output(0)`'s rate (rounded, `int`) and `GetMaxFramesPerSlice()`, sizes the
  per-bus channel-pointer tables, seeds the reconciliation baseline and resets.
  Buffer pointers are taken per render, never in `Initialize`, because
  `DoInitialize` reallocates after it.
- **Render.** `Render` is overridden, not `ProcessBufferLists`. In order: consume a
  pending reset (`wrapper.reset()`), `clearMidi()` and move the MIDI staged since
  the last block onto MIDI-in bus 0 under its spin lock, reconcile parameters,
  `PullInput` per bus (an unconnected bus binds empty), `setPlayhead` from the
  host callbacks, `sortMidiInByOffset`, bind each output (with one output element
  bus 0 is `Output(0).GetBufferList()`, which the SDK already pointed at the
  host's buffers; with more, every bus is `PrepareBuffer(frames)`), with the input
  of the same index as the matching input when it was valid and has the same
  count, `process()`, then drain `midiOut()` into the MIDI output callback. MIDI
  arrives through `MIDIEvent` before `Render`, on any thread, which is why it is
  staged rather than pushed into the wrapper directly.
  `DoRender` already refuses an uninitialized unit, too many frames, and installs
  its own denormal disabler.
- **Reset** is a real reset: `Reset()` sets an atomic flag and calls the base; the
  next render runs `wrapper.reset()`. The SDK does not serialise `Reset` against
  `Render`, so nothing is touched from `Reset` itself. No CC 120 is injected.
- **MIDI in** overrides `HandleNoteOn`, `HandleNoteOff`, `HandleControlChange`,
  `HandlePitchWheel` (14-bit, normalized), `HandleChannelPressure`,
  `HandlePolyPressure`, `HandleProgramChange`, `HandleSysEx` and `HandleNonNoteEvent`,
  the last because `AUMIDIBase` diverts CC 120, 121 and 123 to offset-less no-ops;
  each stages its event under a spin lock for the next render, which pushes it to
  MIDI-in bus 0 at its frame (AU delivers one MIDI stream). `MIDIEventList` stays
  at the SDK default, so a host falls back to `MIDIEvent`.
- **MIDI out** exists when the layout has a MIDI output bus:
  `kAudioUnitProperty_MIDIOutputCallbackInfo` (a `CFArrayRef` of bus names) and
  `kAudioUnitProperty_MIDIOutputCallback` (`AUMIDIOutputCallbackStruct`, published
  through a `RealtimeSwap`). Each render builds a `MIDIPacketList` in a buffer
  sized at `Initialize`, one packet per event stamped with its sample offset,
  flushing when full, and calls the callback with the render timestamp.
- **State** is the SDK's ClassInfo dictionary plus one key, `MakeASoundState`, a
  `CFData` of `wrapper.saveState(StateContext::Session)`; AU has no project/preset
  signal, so `Session` always. `RestoreState` runs the base (which checks the
  codes and restores `Globals()`), then `wrapper.loadState(bytes, Session)`, mirrors
  the plugin's values into `Globals()`, and checks the latency. No exception crosses
  the ABI. Off the message thread the wrapper's save reads the published snapshot,
  which is what made `auval -strict -stress` hang in Plug and does not here.
- **Playhead** is read through `CallHostBeatAndTempo`,
  `CallHostMusicalTimeLocation` and `CallHostTransportState`, the sample time
  being the transport's timeline position, or the timestamp's when there is no
  transport; `isValid` when any answered.
- **The message thread.** The factory function and the constructor call
  `adoptHostMessageThread()`, as the VST3 factory and `initialize` do. AU has no
  module-exit hook, so `EntryPoint.cpp` releases it from a function marked
  `__attribute__((destructor))`, which dyld runs when the bundle unloads.
- **The view** is `kAudioUnitProperty_CocoaUI`: an `AudioUnitCocoaViewInfo` naming
  the bundle (`[NSBundle bundleForClass:]`) and a factory class. The ObjC runtime is
  one per process and every `.component` loaded registers its classes into it, so
  the two classes are **named per plugin**: `CocoaUI.mm` is compiled into each
  `<Name>-AU` module (recorded on `MakeASoundAU` as `MAKEASOUND_AU_VIEW_SOURCE`,
  beside the entry point), with `MAKEASOUND_AU_VIEW_CLASS` defined by
  `makeasound_add_plugin` as a C identifier from the bundle id and version. Where
  the UI tier is not built the property names `NoCocoaUI.cpp` instead, which
  defines the same `cocoaViewInfo()` hook as unsupported, so the choice is one
  source file, not a registering initialiser and `WHOLE_ARCHIVE`. The factory
  reaches the adapter through a custom global property
  (`kMakeASoundAUProperty_Adapter`), the `Adapter*`, which is safe because the
  class name ties the view to the binary that defined it. The `NSView` shows
  `createEditor()` or `GenericEditor` in an `eacp::Graphics::EmbeddedView` over
  itself, `autoresizingMask` from `isResizable()`, `onAttached()` once embedded
  and `onRemoved()` on close. The host owns the view independently of the unit
  (Logic disposes the unit first), so the adapter keeps a closer per open view,
  runs them in its destructor while the wrapper still lives, and the view's
  `dealloc` unregisters. Plug's window-moving heuristics for Logic's view service
  are stage 6.
- **The plist is generated at build time** by `MakeASoundAUPlistGen`, one
  executable (`PlistGen.cpp`) that `dlopen`s the module just linked and calls its
  exported `MakeASoundAUWritePlist`, which hands `describeModule()` to
  `writeAudioComponentsPlist` (`Plist.cpp` in the SDK-free `MakeASoundAUDescribe`)
  to write `Info.plist`: `CFBundlePackageType` `BNDL`,
  `CFBundleSignature` `????`, identifier, name, executable, and
  `CFBundleVersion`/`CFBundleShortVersionString` from **`ModuleDescription::version`**,
  as `Description.h` already promises (macOS caches a bundle's `AudioComponents`
  keyed by `CFBundleVersion`); the CMake `VERSION` argument stays the standalone's
  and the VST3's. `AudioComponents` holds one entry per plugin: `type`,
  `subtype`, `manufacturer` as four-character strings, `name` `"<vendor>: <name>"`,
  `description`, `version` as the integer `major << 16 | minor << 8 | patch` of
  `PluginDescription::version`, `factoryFunction` `MakeASoundAUFactory`,
  `sandboxSafe` true.
- **The SDK** is Apple's AudioUnitSDK (Apache-2.0), fetched through CPM with
  `DOWNLOAD_ONLY` at a pinned tag and built in `ThirdParty/CMakeLists.txt` as the
  static target `ausdk` from its twelve sources, SYSTEM include root, frameworks
  AudioToolbox, CoreAudio, CoreMIDI and CoreFoundation PUBLIC, the SDK's
  warnings off, IDE folder `External/AudioUnitSDK`. Its headers include
  `<expected>`, so `ausdk` carries `cxx_std_23` PUBLIC: `MakeASoundAU`, each
  `<Name>-AU` module and nothing else compile as C++23, and the tests reach the
  adapter through the C API alone. The SDK's render-safety attributes
  (`AUSDK_RTSAFE`) are matched with plain `noexcept override`s.

### Layout

New directory `Lib/MakeASound/Plugin/AU/` (namespace `MakeASound::AU`, IDE folder
`Lib/Plugin`), every TU built as Objective-C++ only where it must be:

| file | what |
| --- | --- |
| `CMakeLists.txt` | `MakeASoundAUDescribe`, `MakeASoundAU`, `MakeASoundAUPlistGen` and the file properties |
| `AUCommon.h` | the SDK includes (`AudioUnitSDK/MusicDeviceBase.h`, `AUMIDIEffectBase.h`), `namespace ausdk` alias |
| `ComponentType.{h,cpp}` | `ComponentInfo componentInfoFor(const ModuleDescription&, const PluginDescription&)` → type, subtype, manufacturer, bus counts; `fourCCString`; SDK-free, AudioToolbox only |
| `Plist.{h,cpp}` | `writeAudioComponentsPlist(module, bundle name, bundle id, executable, output)`; SDK-free |
| `Adapter.{h,cpp}` | the unit |
| `HostParameters.{h,cpp}` | ids, `GetParameterInfo` filling, value strings, text conversions |
| `Conversion.{h,cpp}` | noexcept conversions: playhead from the host callbacks, MIDI bytes to `MIDI::Event` and `MIDI::Event` to packet |
| `State.{h,cpp}` | the custom key in and out of the ClassInfo dictionary |
| `CocoaUI.h` | `CocoaViewInfo cocoaViewInfo()` and the adapter-property id, what the adapter asks the module's view TU |
| `CocoaUI.mm` | the factory and view classes, named by `MAKEASOUND_AU_VIEW_CLASS`; compiled into each module |
| `NoCocoaUI.cpp` | `cocoaViewInfo()` answering none; compiled into each module where there is no UI tier |
| `EntryPoint.cpp` | `MakeASoundAUFactory` and `MakeASoundAUWritePlist`; compiled into each module |
| `PlistGen.cpp` | `MakeASoundAUPlistGen`'s `main`: `dlopen`s a module and calls its `MakeASoundAUWritePlist` |
| `AUExports.txt` | `_MakeASoundAUFactory`, `_MakeASoundAUWritePlist` |
| `PkgInfo` | shared with VST3: the VST3 target's file is reused through its property |

Elsewhere: `ThirdParty/CMakeLists.txt` (the `ausdk` target, under `APPLE AND NOT
IOS`), `CMake/MakeASoundPlugin.cmake` (`AU` format, `_makeasound_add_au`),
`Plugin/CMakeLists.txt` (`add_subdirectory(AU)`), `Plugins/Gain` and
`Plugins/Synth` (`FORMATS Standalone VST3 AU`), `Plugin/Validation/` and
`Tools/PluginValidator/` (auval), `Tests/AUTests.cpp`, `Tests/AUAllocationTests.cpp`,
`Tests/CMakeLists.txt`, `.github/workflows/ci.yml`, `CLAUDE.md`, `README.md`.

**Targets:**

- **`ausdk`** (STATIC, `ThirdParty/`): added with `vst3sdk` under
  `MAKEASOUND_BUILD_PLUGIN` on `APPLE AND NOT IOS`.
- **`MakeASoundAUDescribe`** (STATIC): `ComponentType.cpp`, `Plist.cpp`; links
  `MakeASoundPlugin` PUBLIC and AudioToolbox. C++20.
- **`MakeASoundAU`** (STATIC): added when `TARGET ausdk`; `Adapter.cpp`,
  `HostParameters.cpp`, `Conversion.cpp`, `State.cpp`; links `MakeASoundPlugin`,
  `MakeASoundAUDescribe` and `ausdk` PUBLIC. Properties `MAKEASOUND_AU_ENTRY`,
  `MAKEASOUND_AU_EXPORTS`, `MAKEASOUND_AU_PKGINFO`, `MAKEASOUND_AU_VIEW_SOURCE`
  (`CocoaUI.mm` with `MakeASoundPluginUI` and `eacp-graphics`, else
  `NoCocoaUI.cpp`), and `MAKEASOUND_AU_HAS_VIEW=1` PUBLIC with the view; the view
  TU needs `MakeASoundPluginUI`, `eacp-graphics` and Cocoa, recorded as
  `MAKEASOUND_AU_VIEW_LIBRARIES` for the module to link. Unity build follows
  `MAKEASOUND_UNITY_BUILD`.
- **`MakeASoundAUPlistGen`** (executable, IDE folder `Lib/Plugin`): `PlistGen.cpp`,
  linking nothing of ours; built whenever `MakeASoundAU` is.
- **`<Name>-AU`** (MODULE, IDE folder `<Name>`), by `makeasound_add_plugin`.

### Build

`_makeasound_add_au(name)`, after `_makeasound_add_vst3`'s pattern:

- `add_library(${name}-AU MODULE <entry> <view source>)`, linking `MakeASoundAU`
  then `${name}` PRIVATE (the core defines `describeModule()`), plus the view
  libraries; `MAKEASOUND_AU_VIEW_CLASS=MakeASoundAUView_<id>` where `<id>` is
  `string(MAKE_C_IDENTIFIER "${ARG_BUNDLE_ID}_${ARG_VERSION}")`; `-fobjc-arc` on
  the `.mm`.
- `BUNDLE TRUE`, `BUNDLE_EXTENSION component`, `OUTPUT_NAME`, `PREFIX ""`,
  `LIBRARY_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/AU>`, the bundle path on the
  target as `MAKEASOUND_AU_BUNDLE`, Release LTO, `.pdb`-style separation is moot.
- `-exported_symbols_list` `AUExports.txt` with `LINK_DEPENDS`, so `nm -gU` shows
  exactly `_MakeASoundAUFactory` and `_MakeASoundAUWritePlist`.
- `add_dependencies(${name}-AU MakeASoundAUPlistGen)`; targets are global, so a
  CPM consumer's directory can name it.
- `POST_BUILD`, in order: `$<TARGET_FILE:MakeASoundAUPlistGen>` loads
  `$<TARGET_FILE:${name}-AU>` and writes `Contents/Info.plist` (arguments: module
  binary, bundle name, `<BUNDLE_ID>.component`, executable name, output path);
  `PkgInfo` copied in; `codesign --force --sign -`.
- With `MAKEASOUND_INSTALL_PLUGINS`, `InstallPluginBundle.cmake` into
  `~/Library/Audio/Plug-Ins/Components`.
- Skipped with a status line where `MakeASoundAU` is not a target, so Windows,
  Linux and iOS trees are untouched.

### Validation

`MakeASoundPluginval` gains the AU half, macOS only by TU:

- `findBundles(directory)` also returns every `*.component`.
- `validate(pluginval, bundle, options)` on a `.component` ignores the pluginval
  path: `AUValidator-macOS.cpp` copies the bundle into
  `~/Library/Audio/Plug-Ins/Components` (replacing the one there), reads the
  `AudioComponents` array from the bundle's plist through `CFBundle`, and runs
  `auval -strict -v <type> <subtype> <manufacturer>` per entry through
  `eacp::Processes::run`, concatenating the logs; `Options::stress` (default 0) adds
  `-stress N`. If auval answers that the component was not found, it runs
  `killall -9 AudioComponentRegistrar` once and retries, since the registrar
  caches the scan. `passed` is every entry exiting 0 with `PASS` in its output.
  `AUValidator-Default.cpp` reports a `.component` as failed with "AU validation is
  macOS only".
- `PluginValidator` therefore takes `.component` bundles and folders of them with
  no new flags (`--stress N` is new); `PluginValidator build/VST3 build/AU` is the
  whole sweep. The `fetch` of pluginval still happens only when a `.vst3` is named.
- `Tests/PluginvalTests.cpp`'s env-gated case also sweeps `MAKEASOUND_AU_DIR`.

### Tests

`Tests/CMakeLists.txt`: `if (TARGET MakeASoundAU)` adds `AUTests.cpp`, compiles
`MAKEASOUND_AU_ENTRY` into the test target (the factory, defined once per binary,
over `PluginTests.cpp`'s `describeModule()`, which gains the MIDI-echoing instrument
as a second plugin with its own code so the subtype lookup is exercised; the VST3
cases that count factory classes are updated), links `MakeASoundAU` and
`MakeASoundAUDescribe`, and in the allocation branch adds `AUAllocationTests.cpp`.
The tests never include an adapter header: they stay C++20 and host the unit
through the C API.

**`Tests/AUTestHost.h`**: `registerTestComponents()` calls `AudioComponentRegister`
once per plugin with the exported factory cast to `AudioComponentFactoryFunction`
(instantiating from the returned handle, never `AudioComponentFindNext`, which could
find an installed bundle), and `UnitHost {component, instance, buffers, timestamp,
midiOut}` that sets the stream formats, `MaxFramesPerSlice`, initializes, renders
through `AudioUnitRender` into its own `AudioBufferList`s with a pull-input
callback, and collects MIDI out through the callback property.

**`AUTests.cpp`** (suite `AU/`), after `VST3Tests.cpp`:
- `ComponentType`: the four categories map to their types; the fourcc strings.
- the factory: both plugins instantiate, each unit's description matches its codes,
  and an unknown subtype fails to instantiate.
- channels: `SupportedNumChannels` lists what `acceptsLayout` accepts; mono is
  reachable by setting one element; a refused pair fails `Initialize`.
- a block in place and not: input reaches output through the gain, a bus past the
  input's width is zeroed, a render before `Initialize` is refused; two output
  buses both carry their block when bus 1 is rendered first at a timestamp; the
  playhead is the transport's timeline position, the stream clock without one.
- parameters: the list is the host ids in declaration order; info, value strings,
  string-from-value and value-from-string (non-finite text keeps the current
  value); a host write lands in the next block; the
  edit gate (a held parameter's host write is dropped and mirrored back); the gesture
  events reach an `AUEventListener`.
- state: ClassInfo round trip including the custom key and a parameter inserted
  mid-list, and through `ClassInfoFromDocument`; a dictionary from another subtype is refused; a save from a worker
  thread while the main thread spins returns.
- MIDI: `MusicDeviceMIDIEvent` notes reach the instrument at their offsets, CC 123
  with its frame, SysEx through `MusicDeviceSysEx`, events sent from another thread
  during renders all arrive in order; the echoed events come back
  through the MIDI output callback with their offsets; no callback, no crash.
- `Reset` silences a held note, before any render too.
- latency: a change posts `kAudioUnitProperty_Latency` to a property listener.
- `kAudioUnitProperty_CocoaUI` is answered only with the view, and names a class
  the runtime can find.

**`AUAllocationTests.cpp`**: whole `AudioUnitRender` calls with host parameter
writes, MIDI, a held parameter and variable block sizes, the ban raised inside the
plugin's `process`.

### CI and auval

The macOS job gains a step `auval (Release)` after pluginval:
`build-Release/Tools/PluginValidator/PluginValidator --logs
build-Release/pluginval/logs build-Release/AU`, which installs into the runner's
user folder and runs `auval -strict -v` per component; the log upload already covers
its folder. `PluginValidator build/VST3 build/AU` is the local sweep. No other job
changes: `ausdk`, `MakeASoundAU` and every `-AU` target skip with a status line off
macOS.

### Fallout in this repository

| place | change |
| --- | --- |
| `ThirdParty/CMakeLists.txt` | `ausdk` through CPM, Apple desktop only |
| `Plugin/CMakeLists.txt` | `add_subdirectory(AU)` after `VST3` |
| `CMake/MakeASoundPlugin.cmake` | `AU` format, `_makeasound_add_au` |
| `Plugins/Gain`, `Plugins/Synth` | `FORMATS Standalone VST3 AU` |
| `Plugin/Validation/`, `Tools/PluginValidator/` | `.component` through auval |
| `Tests/` | `AUTests.cpp`, `AUAllocationTests.cpp`, `AUTestHost.h`, the second test plugin |
| `.github/workflows/ci.yml` | the auval step on macOS |
| `CLAUDE.md`, `README.md`, `plan.md` | the format as it is |

No public API change. `MakeASoundPlugin` is untouched.

### Fallout downstream

None: `MAKEASOUND_BUILD_PLUGIN` is off in Plug and tamber-web.

### Done when

- `PluginValidator build/AU` passes `auval -strict` on `MakeASound Gain.component`
  and `MakeASound Synth.component` on this Mac, and the macOS CI step is green.
- `AU/` and the AU allocation cases are green on macOS; every other job is unchanged.
- Both bundles carry `Contents/PkgInfo`, a generated plist with the right
  `AudioComponents`, a valid ad-hoc signature, and `nm -gU` shows exactly
  `_MakeASoundAUFactory` and `_MakeASoundAUWritePlist`.
- Each bundle loads in Logic or Ableton on the Mac, opened by hand after a build
  (installed by default): the generic editor opens; automation records
  and plays back; the synth plays from a MIDI track; a saved project reopens with
  its values; two MakeASound AUs open in one project with editors.
- CLAUDE.md, README and this plan describe the format as it is.

## Later stages, in brief

- **Editors**: on top of stage 3's `Editor` and its `view()`, size and aspect
  policy and the host resize request; a generic parameter page with no npm; a
  React page through the same `miro_export` codegen `Apps/Synth` uses; Plug's
  window heuristics for Logic's AU view service.
- **Bypass in AU**: `kAudioUnitProperty_BypassEffect` mapped onto the plugin's
  `{.bypass = true}` parameter, as VST3's `kIsBypass` flag already does. auval
  warns that the recommended property is missing and passes.
- **CLAP**: the ids and the per-event MIDI path are already prepared; note ids and
  per-note expression are the remaining `MIDI::Event` gap.
- **AUv3**: a new adapter over the same core plus app-extension packaging.

## Decisions log

- 2026-10-10: `Buffer` is one move-only class, owning or referring, offset-based
  sub-ranges. Alternatives weighed: an owner plus `AudioBlock<T>` view (cleaner
  constness, rejected for the second name), JUCE's deep-copying single class
  (rejected for the allocation footgun).
- 2026-10-10: `AudioCallbackInfo` stays a copyable POD holding tables and counts;
  `getInput()`/`getOutput()` build referring `Buffer`s, so `auto out =
  info.getOutput()` stays valid in Plug and tamber-web.
- 2026-10-10: parameter host ids are hashes of stable string ids, not indices.
- 2026-10-10: sample rates stay `int` across `ProcessSpec`; adapters round.
- 2026-10-10: `getBusLayout()` is on `Processor`, not `Plugin`: `Engine` sizes its
  context from it for an app with no plugin. A bus channel the device cannot
  feed reads device channel 0 when the device has inputs at all (a mono mic
  spreads across a stereo bus, as in Plug) and silence otherwise; an output bus
  channel past the device writes into a bin.
- 2026-10-10: `Engine::start` re-prepares on the host thread for the rate and
  period the device actually opened at; the audio-thread re-prepare is kept for
  a recovery that re-opens at a shape nobody prepared for, and is the one
  documented allocation there.
- 2026-10-10: `RealtimeSwap` is two atomic slots with single ownership and a
  message-thread sweep every 250 ms, not Plug's FIFO plus `use_count` reaper: no
  reference count on the audio thread, nothing freed there, and a publish that
  meets a full `retired` slot is delayed a block rather than dropped.
- 2026-10-10: parameters are registered with `add()` in a `ParameterGroup`
  constructor, separate from other reflectable state and explicit; ids default
  to names, with an `id` override on a parameter or a group to keep a key across
  a rename. The reflected-struct walk (`ParameterReflector`,
  `MAKEASOUND_PARAMETERS`) was dropped: Miro's walk carries keys, not C++ types,
  so two instances of one group type could not have their own names; leaving a
  session-only parameter out of a preset needed a specialisation in Miro's
  detail namespace; and Miro's load rebuilt vectors and maps of groups under the
  registry, leaving its pointers dangling.
- 2026-10-10: state saves off the message thread read a published snapshot, preset
  and session both, with the parameters read live, and loads apply parameters
  inline and defer the rest without them, so no host thread ever waits on the
  message thread for a `StatePlugin`'s document. A deferred half older than the
  latest load drops.
- 2026-10-10: `ScopedNoDenormals` and `Smoother` live in the device library's
  `Realtime/`, usable by an app with no plugin; eacp enters `MakeASoundPlugin`
  PRIVATE through one TU.
- 2026-10-10: parameter host ids are 31 bits (VST3 reserves the top half).
- 2026-10-10: the repository is MIT, and the VST3 SDK is vendored rather than
  fetched: 3.8.0 relicensed it to MIT, so a trimmed in-tree copy with its notices
  is clean, builds offline and configures with no network.
- 2026-10-10: `State::version` is the schema written; a load reads the
  document's into `loadedVersion`, so a v2 build loading a v1 document still
  saves v2.
- 2026-10-10: `createEditor` waits for stage 3, which brings the first window;
  `Plugin` carries no placeholder for it.
- 2026-10-10: `RealtimeSwap` and `MessageThread` live in `Plugin/Realtime/`, not
  the device library's `Realtime/`: the swap needs the message thread, eacp
  backs that, and keeping it there keeps eacp out of the device library.
- 2026-10-10: the standalone format drives `PluginWrapper` through a thin
  `Processor`, `Standalone::StandaloneProcessor`, rather than giving `Engine` a
  wrapper path: `Engine` is in the device library, which must not know the plugin
  core, and the adapter is the same sequence of wrapper calls VST3 and AU will
  make, so the standalone exercises the pipeline as they will.
- 2026-10-10: host-side UI (the generic editor, the settings panel) is eacp-ui
  GPU widgets in `ComponentHost` trees, not web pages: no npm, no JS bridge and
  no embedded resources in every plugin binary, and the `UI::DropdownInfo`
  helpers the web demos use feed the widgets directly. A plugin's own editor may
  still be a `WebView`.
- 2026-10-10: `Editor.h` carries only a forward declaration of
  `eacp::Graphics::View`, so `MakeASoundPlugin` still includes no eacp header and
  links no GUI tier; `MakeASoundPluginUI` and `MakeASoundStandalone` do, and an
  adapter that hosts an editor links them.
- 2026-10-10: the standalone settings store devices and MIDI ports by name and
  re-resolve them on launch (`resolveConfig`, `resolvePortIds`): both id
  registries are per launch, so a saved id names nothing next time.
- 2026-10-10: `MidiSender` is stopped around opening or closing the MIDI output
  rather than `MidiManager` guarding a send against a port change: the guard
  would sit on every backend's send path for the one caller that sends from a
  thread of its own.
- 2026-10-10: a standalone starts output-only whatever its layout; an effect's
  input is the user's explicit pick. The first `Gain` run opened the default
  microphone into the default speakers and fed back.
- 2026-10-10: VST3 is one `SingleComponentEffect` per plugin (`VST3::Adapter`) driving `PluginWrapper` exactly as `StandaloneProcessor` does. Its `ParamID` is the parameter's 31-bit host id, looked up with `indexOfHostId`. Controller values are read live from the plugin, so the controller cache cannot go stale. `ProxyParameter` converts through the MakeASound `Parameter`, so skewed ranges display correctly.
- 2026-10-10: VST3 state is one document through `IComponent::getState`/`setState`. It is a `Session` unless the stream's `IStreamAttributes` names a non-project state type, in which case it is a `Preset`. The editor-state pair stays `kNotImplemented`, and `setComponentState` reads nothing, since the component and the controller are one object.
- 2026-10-10: MIDI CC, channel aftertouch, pitch bend and program change reach a VST3 plugin through `IMidiMapping` onto hidden shadow parameters `0x7FFF0000 | bus << 12 | channel << 8 | controller` (controllers 0–130, at most 16 buses), expanded point by point into `MIDI::Event`s. They are registered whenever the layout has MIDI in, with no flag. A plugin parameter whose host id falls in the range wins, and asserts in debug. Plug's bit-31 marker was dropped because VST3 reserves those ids.
- 2026-10-10: the VST3 class id is `FUID(manufacturerCode, pluginCode, 'VST3', 0)`, Plug's derivation, so sessions saved with a Plug build find the MakeASound build of the same plugin. The class carries no `kDistributable` flag.
- 2026-10-10: `MakeASoundVST3` is one static target. It compiles its `IPlugView` over `EmbeddedView` and links `MakeASoundPluginUI` when that target exists, and a null view otherwise. The choice is at link time, with no static registrar and no `WHOLE_ARCHIVE`. `MakeASoundPlugin` still sees neither the SDK nor eacp-ui.
- 2026-10-10: a VST3 adapter prepares the wrapper in `setActive(true)`, not `setupProcessing`, so the layout is final when it allocates. A `process` before the first activation renders silence.
- 2026-10-10: a module loaded by a foreign host calls `adoptHostMessageThread()` from the factory's create function and from `initialize`. On Linux the eacp loop is pumped from the host's `IRunLoop`, given either as the factory's host context or by the view's frame. Without one, deferred work runs only while a view is open.
- 2026-10-10: the bundle plist comes from CMake arguments (`OUTPUT_NAME`, `BUNDLE_ID`.vst3, `VERSION`, `COMPANY`) at configure time, not from `describeModule()`: VST3 hosts read identity from the factory. AU, whose registry reads the plist, gets a generator in stage 5.
- 2026-10-10: a `.vst3` is a folder bundle on all three platforms, in `<build>/VST3/`. The exports are sealed on macOS (the SDK's `macexport.exp`) and on Linux (a version script) so a host and its plugins never coalesce each other's C++. `PkgInfo` and an ad-hoc signature are added on macOS. Copying into the user's plug-in folder is opt-in (`MAKEASOUND_INSTALL_PLUGINS`) and best-effort.
- 2026-10-10: pluginval runs as the executable `PluginValidator`, on the library `MakeASoundPluginval`, which fetches v1.0.4 through eacp's `OnlineResource` (once, into eacp's resource folder) and runs it through `Processes::run`; a test case does the same when `MAKEASOUND_PLUGINVAL` is set. It replaced a `cmake -P` script behind a custom target: a build step that runs a validator is the wrong shape, and a runnable target is what CLion, CI and a test can all drive. The tool depends on no plugin target and has no compiled-in folder: the bundles and folders to validate are its arguments. CI runs it at strictness 10 per push on macOS, Windows x64 (MSVC and clang-cl) and Linux GCC, Linux under `xvfb-run` with `--skip-gui-tests`. Not on Windows ARM64, which has no pluginval build.
- 2026-10-10: platform code is split by TU, as eacp does, never by `#if`:
  `MessageThread-{Linux,Default}.cpp`, `HostRunLoop-{Linux,Default}.cpp` and
  `PlugView-{macOS,Windows,Linux}.cpp`, each picked in CMake. `HostRunLoop` is the
  seam that makes the factory's and the view's run-loop hand-over the same call on
  every platform, with a null answer where there is nothing to attach to.
- 2026-10-10: AU is one `MusicDeviceBase` class, `AU::Adapter`, for every
  component type, behind one exported factory symbol per module,
  `MakeASoundAUFactory`, which picks the SDK's dispatch table from the component
  type and refuses a foreign manufacturer or an unknown subtype; Plug's four
  factories were dropped. The instance finds its plugin by its own subtype, so a
  module's plugins share one bundle.
- 2026-10-10: an `AudioUnitParameterID` is the parameter's 31-bit host id, as a
  VST3 `ParamID` is, so `Globals()` runs in map mode, seeded with every exposed id
  in `PostConstructor` so the render thread never inserts, and `SetParameter`
  refuses any other id. `CanScheduleParameters()` is false, because the SDK's
  scheduled path allocates on the render thread.
- 2026-10-10: AU parameter reconciliation keeps two baselines per exposed entry,
  what the host and what the plugin held after the last block, and writes
  `Globals()` back only when the plugin moved on its own or a held parameter's
  host write is undone. A host's value is never replaced by the plugin's snapped
  form of it, which auval's "retain set value" check requires.
- 2026-10-10: the AU view's Objective-C classes are named per module
  (`MakeASoundAUView_<bundle id>_<version>`) by compiling `CocoaUI.mm` into each
  `<Name>-AU` with the name defined, and a module without a UI tier compiles
  `NoCocoaUI.cpp` instead; the choice is which source the module compiles, not a
  registering initialiser in the static library kept alive by `WHOLE_ARCHIVE`.
  One ObjC runtime serves the whole host process, so two components sharing a
  class name would share one view. The name ties the view to its own binary,
  which is what makes handing it the `Adapter*` through a custom property safe.
- 2026-10-10: the AU `Info.plist` is generated after each link by
  `MakeASoundAUPlistGen` from the module's `describeModule()`, and its `CFBundleVersion` is
  `ModuleDescription::version`, not the CMake `VERSION`: macOS caches a bundle's
  `AudioComponents` keyed by that version, so it has to move with the plugin
  list the module declares.
- 2026-10-10: the AudioUnitSDK is fetched through CPM at the tag
  `AudioUnitSDK-1.4.0` (Apache-2.0) rather than vendored, and built as `ausdk`
  on `APPLE AND NOT IOS`. Its headers need C++23 (`<expected>`), which `ausdk`
  carries PUBLIC, so only what links it, the AU adapter, its modules and the
  tests' `MakeASoundAUTestModule`, compiles as C++23; `MakeASoundPlugin`, `MakeASoundAUDescribe` and
  the plist tool stay C++20 and no adapter header is included elsewhere.
- 2026-10-10: auval runs through `PluginValidator`, the same tool and library as
  pluginval: a `.component` is installed into `~/Library/Audio/Plug-Ins/Components`
  first, since auval only finds installed components, then each `AudioComponents`
  entry runs `auval -strict -v`, the registrar restarted once if it has not seen
  the new bundle. CI runs it on the macOS job after pluginval.
- 2026-10-10: AU `Reset` is a real reset: it raises a flag the next render
  consumes with `wrapper.reset()`, because the SDK does not serialise `Reset`
  against `Render`. Plug's injected CC 120 was dropped; `Processor::reset`
  exists for this.
