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
| 3 | Standalone format | a `Plugin` runs in a window with device and MIDI pickers |
| 4 | VST3 | pluginval at strictness 10 |
| 5 | AU | auval after an install step |
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
- **Examples**: `Examples/Gain` (effect, the smallest possible) and
  `Examples/Synth` (instrument, ported from `Apps/Synth`).

Proof: both examples run as standalone apps on macOS with device, sample rate,
block size and MIDI port pickers; `Synth` plays from a hardware MIDI port and the
typing keyboard; settings and state survive a relaunch; the per-example allocation
test (the harness from `Tests/AllocationProbe.h`) passes under a live callback.

## Later stages, in brief

- **VST3**: SDK through CPM `DOWNLOAD_ONLY` with a pinned tag and a small in-house
  target, single-component `IComponent`/`IAudioProcessor`/`IEditController`,
  `IMidiMapping` shadow parameters expanded point by point into CC events,
  `IPlugView` over `eacp::Graphics::EmbeddedView`, bundle with `PkgInfo`, sealed
  exports, generated plist, pluginval in CI. The VST3 SDK is GPLv3 or Steinberg's
  proprietary licence, which matters once this repository has a licence of its
  own.
- **AU**: AudioUnitSDK through CPM, `ausdk::AUBase` family factories chosen by
  category, parameters reconciled through `GetParameterRT`, MIDI output
  callback, `kAudioUnitProperty_CocoaUI` view factory over `EmbeddedView`,
  `AudioComponents` plist written by a build-time generator that links the plugin
  core and calls `describeModule()`, `auval` in CI after an install step.
- **Editors**: on top of stage 3's `Editor` and its `view()`, size and aspect
  policy and the host resize request; a generic parameter page with no npm; a
  React page through the same `miro_export` codegen `Apps/Synth` uses.
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
- 2026-10-10: `State::version` is the schema written; a load reads the
  document's into `loadedVersion`, so a v2 build loading a v1 document still
  saves v2.
- 2026-10-10: `createEditor` waits for stage 3, which brings the first window;
  `Plugin` carries no placeholder for it.
- 2026-10-10: `RealtimeSwap` and `MessageThread` live in `Plugin/Realtime/`, not
  the device library's `Realtime/`: the swap needs the message thread, eacp
  backs that, and keeping it there keeps eacp out of the device library.
