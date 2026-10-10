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
| 2 | `MakeASoundPlugin`: `Plugin`, parameters, state, description | fake-host tests drive a plugin end to end |
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
  Declared as a `MIRO_REFLECT`ed struct; a `ParameterReflector` walks it to build
  the registry, nested structs become id and display prefixes, and the same
  reflection serialises state. The host id is a 32-bit hash of the stable string
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

## Stage 3: the standalone format, the first provable step

The first format, because it needs no SDK, exercises every MakeASound seam, and
is what Plug's `Standalone/` already proves works on top of this library.

- **`Plugin/Standalone/`**: `StandaloneApp` built on `eacp::Apps::run<T>`, a
  `Window` whose content is the editor's view or a generic parameter page,
  `Engine` from stage 1 driving the `PluginWrapper`, a settings panel built from
  `UIDeviceManager` and `UIMidiManager`'s dropdowns and toggle lists, settings
  persisted as the already-reflected `StreamConfig` plus open MIDI ports in
  `FilePath::appSupportDirectory()`, a typing keyboard feeding MIDI through an
  `SPSCQueue`, and state saved on quit and restored on launch.
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
- **Editors**: `Editor` with `view()`, size and aspect policy, host resize
  request; a generic parameter page with no npm; a React page through the same
  `miro_export` codegen `Apps/Synth` uses.
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
