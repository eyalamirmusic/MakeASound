# Plan: a plugin framework on MakeASound

MakeASound grows from a device library into a write-once plugin framework: one
`Plugin` compiled to VST3, AU, AAX, AUv3 and a standalone app, later CLAP. The design
follows Plug (the format-neutral core, thin per-format adapters, eacp for
windows and editors, Miro for state and UI schemas), re-homed on MakeASound so the
standalone format and the device library are one code base rather than two.

Stages 0 to 9 have landed on `develop`: `Buffer`, `Processor`/`Engine`, the plugin
core, the standalone, VST3, AU, editor size policy and the web editor, AAX on macOS,
the windowless and iOS standalone, and AUv3 on iOS. CLAUDE.md describes what exists
and the decisions log below keeps why. What remains is the hand checks those stages
could not make in CI, a few follow-ups, stage 10, and the later stages.

Branches: each piece of work is a feature branch cut from `develop` with its pull
request into `develop`; `main` takes `develop` alone. Downstream consumers pin
`gh:eyalamirmusic/MakeASound#main` (Plug, tamber-web), so nothing merges to `main`
until the sites a change breaks are updated with it.

| stage | deliverable | proof |
| --- | --- | --- |
| 10 | parameter and state work from a JUCE comparison: plugin-side changes reaching every host, a dirty signal, a bypass for every effect, skewed ranges and a stable AU order before anything ships, then groups, presets and migration hooks; JUCE session compatibility as a separate opt-in | the format suites cover each fix; pluginval 10, `auval -strict` and aaxval stay green; a host lane follows a plugin-side change in Logic, Ableton and Pro Tools |

## Open from stages 4 to 9

### Hand checks

Nothing in CI can host a DAW, Logic's remote AU view, Pro Tools or an iOS device, so
these are the user's to make, on code that has landed. A failure is a fix in that
format, recorded in the decisions log.

- **VST3 in a DAW on the Mac** (both bundles, installed by the default
  `MAKEASOUND_INSTALL_PLUGINS`): the generic editor opens, resizes and drives
  automation; automation plays back; the Synth plays from a MIDI track and answers
  the mod wheel and CC 123; a saved project reopens with its values. In Ableton the
  Synth's React editor opens, its knobs move the parameters and a host automation
  lane moves the knobs, and a host's own grip resizes the editors.
- **AU in Logic or Ableton**: the generic editor opens; automation records and
  plays back; the Synth plays from a MIDI track; a saved project reopens with its
  values; two MakeASound AUs open in one project with editors. The Synth's React
  editor opens in Logic and Ableton, and dragging the corner grip resizes both AU
  editors in Logic.
- **Logic's view service**: an editor opened near the bottom of the screen ends up
  whole on it, moved where Logic allows and shrunk once where it does not, with
  `MAKEASOUND_AU_VIEW_TRACE` showing which, which also settles whether Logic 11
  still behaves as Plug's heuristics assume; Gain's bypass button in Logic's
  plugin header follows the parameter both ways.
- **The standalone on the Mac**: the Synth's React editor opens in `MakeASound
  Synth.app` and the typing keyboard plays while the page has focus.
- **Windows**: the Synth's web editor opens in the standalone and in a VST3 host,
  and on a machine without the WebView2 runtime the generic editor opens instead.
- **Pro Tools Developer** (retail Pro Tools loads no plugin `wraptool` has not
  signed). Open going in: the validator reports `category: None` for
  `AAX_EPlugInCategory_Effect` (Gain's default), and its `s6_feature` test shows
  `master_bypass support: 0`, which may mean Pro Tools wants the bypass parameter
  flagged as the category bypass too; whether Pro Tools' bypass button drives
  Gain's bypass parameter decides it. The check:
  - both plugins appear under their categories (Effect for Gain);
  - the generic editor (Gain) and the web editor (Synth) open at the top-left of
    the window at their right size, and the grip resizes them;
  - closing and reopening an editor, and removing the plugin with its window open,
    do not crash;
  - automation records and plays back, and the editor's values follow it;
  - the Synth's web page takes keyboard focus for its fields while Pro Tools'
    shortcuts still work outside it;
  - the Synth plays from a MIDI track;
  - a saved session reopens with its values;
  - Gain's bypass follows the parameter and Pro Tools' bypass button;
  - the view finds its plugin (the `dynamic_cast` of `GetEffectParameters()` to
    `Adapter` succeeds in Pro Tools' process; an editor at all proves it).
- **iOS**: the standalone's `Settings` screen tapped through in the simulator
  (CI only launches the apps); both examples' standalones on a device; and in AUM
  and GarageBand on a device, from a `MAKEASOUND_IOS_TEAM` Xcode build, both
  extensions load, render and open their editors, the Synth's web page included.

### Follow-ups

- **iOS interruptions, in the device library.** Recovery retries every 250 ms
  through an interruption instead of waiting for `InterruptionEnded`, and that
  notification's should-resume option is not consulted.
- **The iOS standalone** has no on-screen keyboard and no Bluetooth MIDI pairing
  (`CABTMIDICentralViewController`).
- **AAX transport**: `barStartPpq` and `isRecording` are not mapped.
- **PACE signing** (`CMake/SignAAXBundle.cmake`) has not yet been seen to work
  from a non-interactive shell: wraptool reported no password there after a
  terminal `wraptool sync`.
- **The AAX SDK fork** (`github.com/audiosdk/aax`, pinned by commit) is replaced by
  a mirror of our own if it moves or disappears; only the CPM line changes.
- **eacp.**
  - A top-left option on `EmbeddedView`, which would replace the AAX view's
    autoresizing mask pinning eacp's surface to the container's top-left.
  - `isDLL()` false on iOS and Android, which would let a framework carry eacp
    should one ever be wanted; proposed to eacp, not made.
  - 13 of eacp's own cases fail in the iOS simulator, and eacp's CI does not run
    its iOS tests.
  - The iOS `EmbeddedView` is in eacp's `develop` (eyalamirmusic/eacp#126), which
    `Findeacp.cmake` fetches; a pin, if one is introduced, must name that merge or
    later.

## Stage 10: parameters and state, from a JUCE comparison

A read-only study of JUCE 8.0.12 (`~/Code/JUCE`) against MakeASound's parameter and
state paths found five correctness gaps, three choices that break saved automation
if changed after a plugin ships, a set of cheap adapter features, some later work,
and what a JUCE plugin ported here would need to keep its users' sessions. JUCE
paths below are relative to `~/Code/JUCE/modules/`, MakeASound paths to
`Lib/MakeASound/Plugin/`; line numbers are approximate. The packages run in the
order given: A fixes what is wrong today, B settles what cannot change once a
plugin ships, C is cheap adapter work, D is later, and E, JUCE compatibility, is a
separate opt-in that nothing else waits for. The proof is the format suites
covering each fix, pluginval 10, `auval -strict` and aaxval green on both examples,
and a hand check that a host lane follows a plugin-side change in Logic, Ableton
and Pro Tools.

Where MakeASound is already ahead of JUCE, and stays so: preset versus project
through VST3's `kStateType` and AUv3's `fullStateForDocument`;
`ClassInfoFromDocument`; AAX `CompareActiveChunk` on the real parameters;
off-thread saves from the published snapshot; versioned, tolerant JSON; stamped
load ordering; message-thread-only listeners; gesture-held parameters dropping
host writes; VST3 edit groups and `kIsList`; the RT-safe AUv3 tree sync; collision
asserts on host ids.

### A. Correctness fixes

- **WP10.1, plugin-side parameter changes reach VST3 and AU v2 hosts.** A preset
  loaded in the plugin's own browser, a randomize, a MIDI-learn or a dependent
  parameter moves the plugin and tells no host. JUCE's `setValueNotifyingHost`
  (`juce_audio_processors_headless/.../juce_AudioProcessorParameter.cpp:59-63`)
  reaches VST3 through `CachedParamValues`, flushed off the message thread into
  `data.outputParameterChanges` in the next process
  (`juce_audio_plugin_client_VST3.cpp:574-585, 1472-1488, 3736-3747`), and AU
  through `AUEventListenerNotify(kAudioUnitEvent_ParameterValueChange)`
  (`juce_audio_plugin_client_AU_1.mm:1320-1338`). MakeASound's VST3 adapter sends
  nothing (`VST3/Adapter.cpp` restarts only for latency and titles), and the AU
  reconcile writes `Globals()` silently (`AU/Adapter.cpp` ~778-786), so
  `AUParameterListener`s (Logic's generic view, automation) miss it. AAX
  (`TimerWakeup`) and AUv3 (the 30 Hz tree sync) already do this.
  - *VST3*: a per-parameter baseline like AU's; after `pluginWrapper.process()`
    one point at offset 0 in `outputParameterChanges` for each exposed parameter
    no gesture holds that moved.
  - *AU v2*: the reconcile sets an atomic moved bit per slot, and a
    message-thread timer posts `kAudioUnitEvent_ParameterValueChange` for each.
    Never from the render thread, which JUCE does and which is not RT-safe.
  - *Bulk*: `Plugin::notifyHostParameterValuesChanged()` (message thread) for a
    change to many: VST3 `restartComponent(kParamValuesChanged)`, AU a value event
    per exposed parameter, AAX mirror all, AUv3 `syncTreeFromPlugin`.
- **WP10.2, VST3 `setState` sends `kParamValuesChanged`.** JUCE does after every
  load (`VST3.cpp:1194-1226`); `VST3/Adapter.cpp` ~182-197 only checks the latency.
  One call, marshalled to the message thread as the latency notification is.
- **WP10.3, a dirty signal for state that is not a parameter.** JUCE's
  `ChangeDetails::nonParameterStateChanged` becomes VST3
  `IComponentHandler2::setDirty(true)` (`VST3.cpp:1555, 1614-1619`), AU
  `PropertyChanged(kAudioUnitProperty_ClassInfo)` (`AU_1.mm:1991`) and AAX
  `numSetDirtyCalls` added in `GetNumberOfChanges` (`AAX.cpp:990-993, 1285-1286`).
  `State::markChanged()` only republishes. `HostEditListener` gains
  `stateChanged() noexcept {}`, raised by `StatePlugin` on `markChanged` (message
  thread) and mapped as above in each adapter; AUv3 has no counterpart and ignores
  it.
- **WP10.4, the AUv3 state's `version` is 0.** `AUv3/State.mm` writes `version` as
  `versionNumber(PluginDescription::version)`, but in a ClassInfo dictionary
  `version` (`kAUPresetVersionKey`) is the state format's version and the
  AudioUnitSDK requires 0 (`AUBase.cpp:1818-1824`), so a session saved by our AUv3
  (iPad Logic) fails in our own v2 component on a Mac with
  `kAudioUnitErr_InvalidPropertyValue`. Write 0. The v2 `RestoreState` also
  normalises a `version` of 1 to 0 before the base call, as JUCE does
  (`AU_1.mm:863-923`), since that is what an `AUAudioUnit`'s own state carries.
  Nothing has shipped, so no saved state carries the old value.
- **WP10.5, a bypass for every effect.** AUv3 answers `shouldBypassEffect` NO and
  ignores the write, and AAX has no master bypass, for a plugin with no bypass
  parameter. JUCE synthesises one: VST3 `"byps"` (`VST3.cpp:678-713`), AAX Master
  Bypass (`AAX.cpp:1724-1737`), and a default `processBlockBypassed` that clears
  the extra outputs and asserts a latency of 0
  (`juce_audio_processors_headless/.../juce_AudioProcessor.cpp:590-603`). When
  `bypassIndex() < 0` and the layout has a main input, `PluginWrapper` registers a
  `BoolParam` of its own (`.id = "__bypass"`, `.bypass = true`) as one more exposed
  entry; while it is on, `process()` skips the plugin and runs `virtual void
  Plugin::processBypassed(ProcessContext&) noexcept`, whose default copies the main
  input to the main output through a `latencySamples()` delay (the vendored SDK's
  `public.sdk/source/vst/vstbypassprocessor.h` is a reference) and passes MIDI
  through. Every adapter already maps `Entry::bypass`, so the formats get it with
  no change, latency-compensated, which JUCE's is not.

### B. Settle before anything ships

Each of these changes saved automation once a plugin is out, so it is decided and
landed before one is.

- **WP10.6, the AU parameter list's order.** Logic and GarageBand address an AU's
  parameters by index, so a parameter inserted mid-list breaks their automation
  though every id is stable; the mid-list cases today cover state only. JUCE has
  `ParameterID {paramID, versionHint}`
  (`juce_audio_processors_headless/.../juce_AudioProcessorParameterWithID.h:43-71`),
  sorts the list by id and then `stable_sort`s it by version hint
  (`AU_1.mm:2031-2061`), and asserts in debug when hints are 0 in an AU build
  (`juce_AudioProcessor.cpp:440-452`). `ParameterOptions` gains `versionHint`
  (`int`); the v2 list is ordered by `(versionHint, hostId)`, the AUv3 tree
  considered for the same, with a debug assert when hints are unset. WP10.9 lists
  non-automatable parameters too, so toggling `automatable` stops reshuffling it.
- **WP10.7, ranges with a curve.** `FloatParam`'s `toNormalized` is linear
  (`Parameters/Parameter.cpp` ~45-66) and `HzParam` is "a linearly mapped
  frequency" (`Parameters/CommonParams.h:31`), so a 20 Hz–20 kHz knob is unusable
  in a host lane or an editor; AU and AUv3 publish the plain range with unit
  `Generic`. (The stage 4 note that skewed parameters display correctly in a VST3
  host holds for the conversion path, but no parameter is skewed yet.) JUCE's
  `NormalisableRange` has a skew, a symmetric skew, interval snapping and
  `setSkewForCentre` (`juce_NormalisableRange.h:62-210`), and its AU publishes
  0..1 with `IsHighResolution` (`AU_1.mm:1036-1106`).
  - `struct FloatRange { float min = 0, max = 1, interval = 0, skew = 1; bool
    symmetricSkew = false; static FloatRange withCentre(min, max, centre); }` and
    `FloatParam(name, FloatRange, default, ParameterOptions)`; `HzParam` centred
    at √(min·max). `toNormalized`/`toPlain` stay virtual for a custom curve.
  - `IntParam(name, min, max, default, options)`, `numSteps` `max - min` and
    integer text; `interval > 0` makes a stepped float.
  - AU: the plain range plus `kAudioUnitParameterFlag_DisplayLogarithmic`,
    `DisplaySquareRoot` or `DisplayCubed` when the curve matches one exactly, 0..1
    otherwise; `kAudioUnitParameterFlag_IsHighResolution` always (`AU_1.mm:1061`,
    `juce_audio_plugin_client_AUv3.mm:1188`).
- **WP10.8, path-hashed host ids, documented.** The host id is FNV-1a of the
  group-prefixed id path, so moving a parameter into another group changes its
  host id and its state key unless `id` or `hostId` is pinned; JUCE hashes the
  leaf id, unique across the tree. Decided: keep FNV-1a over the path as the
  default (JUCE's ×31 hash has structured collisions, "Aa" and "BB" among them),
  and say prominently in CLAUDE.md, README and `ParameterOptions`' comment that ids
  are pinned before a parameter is regrouped.

### C. Cheap adapter work

- **WP10.9, "not automatable" apart from "hidden".** JUCE still publishes a
  non-automatable parameter: VST3 without `kCanAutomate` (`VST3.cpp:922`), AU with
  `kAudioUnitParameterFlag_NonRealTime` (`AU_1.mm:1063-1068`), AAX with automatable
  false (`AAX.cpp:1764`). `isHostExposed` hides one entirely. `ParameterOptions`
  gains `hidden`, never published; `automatable = false` becomes published and not
  automatable in every format.
- **WP10.10, groups as VST3 units and AU clumps.** JUCE makes a VST3 `IUnitInfo`
  unit per group, its id the group id's hash `& 0x7fffffff` (`VST3.cpp:419-445,
  548-563`, `info.unitId` at 914), asserting uniqueness and `!= kRootUnitId`
  (668-680), and an AU clump per group (`kAudioUnitParameterFlag_HasClump`, a
  1-based `clumpID`, `AU_1.mm:1078-1084`, and `kAudioUnitProperty_ParameterClumpName`
  at 532-545). MakeASound puts everything in `kRootUnitId` and has no clumps.
  `ParameterList::Entry::groups` already carries the data and
  `SingleComponentEffect` derives from `EditControllerEx1`, so `addUnit` is enough.
  AUv3 has its groups already; AAX pages are skipped.
- **WP10.11, AAX `parameterInfoChanged`.** JUCE's `syncParameterAttributes`
  (`AAX.cpp:2193-2218`) updates the name, type and steps of the live
  `AAX_IParameter`s; do the same with `SetName`, `SetType` and `SetNumberOfSteps`.
- **WP10.12, factory presets.** There are none. JUCE: VST3 a "Factory Presets"
  program list on the root unit plus a `kIsProgramChange` parameter `'prst'`
  (`VST3.cpp:410-483, 720-736, 1036-1056`); AU `GetPresets`/`NewFactoryPresetSet`
  (`AU_1.mm:1612-1657`, the `PresentPreset` change at 1993-1996); AUv3
  `factoryPresets`/`currentPreset` (`AUv3.mm:256-269, 1446-1464`). Shape:
  `Plugin::factoryPresets()` returning a span of `PresetInfo {name, document}` plus
  the current preset, a load being `loadState(document, Preset)`. AUv3 user presets
  (`supportsUserPresets`, `saveUserPreset:`) on top.
- **WP10.13, VST3 parameter migration.** `IRemapParamID::getCompatibleParamID` and
  `IPluginCompatibility`/moduleinfo.json's "Compatibility" (`VST3.cpp:844,
  1104-1141, 603-640`; `VST3/juce_VST3ModuleInfo.h:54-143`;
  `VST3ClientExtensions::getCompatibleParameterIds`). A plugin hook
  `compatibleParameterIds(oldClassId)` returning a map from the old `uint32` id to
  a parameter, and a list of compatible class ids.
- **WP10.14, a gesture helper and an attachment.** An RAII
  `ParameterGesture(plugin, param)` in `MakeASoundPlugin` (begin in the
  constructor, end in the destructor, `set(plain)`, a static `setOnce`), and a
  `ParameterAttachment` in `MakeASoundPluginUI` fed by one shared 30 Hz poll that
  never touches the control being dragged; `GenericEditor` (~148-173) and
  `Web/ParameterApi.cpp` (~50-90) are rebased on them. JUCE's analogue is
  `juce_ParameterAttachments.h:55-104`.
- **WP10.15, a sturdier VST3 state read.** JUCE (`VST3.cpp:2960-3024`) asks
  `ISizeableStream` for the size, accepts a read status other than `kResultOk`
  when bytes arrived (Wavelab), works around Cubase 9's wrong size and reads in
  4 KiB chunks. Port that into `readAll`, with a guard against an absurd size.
- **WP10.16, to consider: `setParamNormalized` while playing.** The VST3 adapter
  writes the processor unconditionally (`VST3/Adapter.cpp` ~485-495); JUCE writes
  it only while not playing (`VST3.cpp` ~962-968), so a UI-thread write does not
  race the process queue during automation playback.
- **WP10.17, an AAX native-id hook.** `nativePluginId` is `pluginCode` plus the
  stem pair's index (`AAX/Describe.cpp` ~198-201), MakeASound's own derivation, not
  JUCE's (JUCE: `AAXClientExtensions::getPluginIDForMainBusConfig`,
  `juce_audio_processors_headless/utilities/juce_AAXClientExtensions.cpp:38-165`,
  and WP10.20). Expose it as an overridable hook. CLAUDE.md calls the derivation
  "JUCE's"; that sentence is corrected when this lands.

### D. Later, optional

- **WP10.18, the rest, each on its own when wanted.**
  - Meter parameters: `MeterParam` with `MeterKind {Input, Output, GainReduction,
    ExpanderGain, Analysis, Other}`; VST3 `kIsReadOnly` and the output queue, AU
    `MeterReadOnly`, AAX `AddMeterDescription` and a meter tap, which changes
    `AlgorithmContext`'s ABI (JUCE `AudioProcessorParameter.h:237-258`,
    `AAX.cpp:415-428, 1446-1447, 2391-2394`). Pro Tools' gain-reduction meter is
    the payoff.
  - `ParameterOptions::meta` → AU and AUv3 `kAudioUnitParameterFlag_IsGlobalMeta`
    (`AU_1.mm:1075-1076`).
  - A `ParameterOptions::unit` enum → AU unit types (dB, Hz, seconds, percent),
    set by `DecibelParam` and the rest.
  - `valueToText(plain, maxChars)`, and generated 31-, 15-, 7- and 4-character
    short names for AAX truncated by UTF-8 code point; `AAX/Parameters.cpp`
    ~86-97 truncates by byte today.
  - Sample-accurate automation, filling `ProcessContext::paramChanges` from VST3
    queue points and AUv3 parameter and ramp events (JUCE takes the last value
    only).
  - `Smoother::follow(const FloatParam&)` glue.
  - `isBoolean()`/`isList()` virtuals instead of inferring from `stateFormat()`.
  - `ParameterOptions::inverted` (AAX orientation), only on request.
  - `textToValue` returning `std::optional<float>`, so text that is no number keeps
    the current value (VST3 `fromString` answering `kResultFalse`); the base maps
    it to the default today (`Parameter.cpp` ~91-94), though AU and AAX already
    keep the current value at their boundary.

### E. JUCE compatibility, opt-in

The goal: a JUCE plugin ported to MakeASound keeps its users' sessions, automation
and presets. Today it keeps nothing in any format. This is a separate opt-in per
module or plugin; nothing in A to D depends on it.

- **WP10.19, identity: parameter ids.** A per module or plugin `IdScheme
  {MakeASound, JUCE, JUCELegacyIndex}`, plus `juceStudioOneMask` (default true);
  the porting author is asked for `JUCE_VST3_CAN_REPLACE_VST2`,
  `JUCE_FORCE_USE_LEGACY_PARAM_IDS` and `JUCE_USE_STUDIO_ONE_COMPATIBLE_PARAMETERS`.
  - JUCE's `String::hashCode` is `h = 31*h + codepoint` over UTF-8-decoded code
    points, `uint32`, from 0; `hashCode64` is ×101 in `uint64`
    (`juce_core/text/juce_String.cpp:562-580`). VST3 and AU ids are
    `hashCode(leafId) & 0x7fffffff` (`juce_VST3ClientExtensions.cpp:43-51`,
    `AU_1.mm:2387-2398`); an AUv3 address is `hashCode64` unmasked, its identifier
    the bare leaf id and a group's identifier the group's own id
    (`AUv3.mm:1235-1287, 1781-1787`); an AAX `AAX_CParamID` is the id string itself
    (`AAX.cpp:1744-1748`), at most 31 characters and unique after truncation
    (asserted, `juce_AudioProcessor.cpp:455-483`). Legacy mode indexes every
    parameter (AU `UseIndexedParameters`; AAX `"0"`, `"1"`, …).
  - `ParameterOptions::hostKey`, the leaf key, defaulting to the parameter's id and
    unique across the tree in this mode; a `hostId` at or above 2^31 allowed when
    masking is off; a 64-bit AUv3 address; a per-parameter `aaxId` override.
  - The AU value range in JUCE mode: 0..1 continuous, 0..numSteps-1 discrete, the
    default normalized × max (`AU_1.mm:1037-1042, 1103-1105`; `AUv3.mm:1249-1253`),
    every parameter listed (a non-automatable one `NonRealTime`), and normalize and
    denormalize equal to JUCE's `NormalisableRange`, skew included (WP10.7).
- **WP10.20, identity: wrapper parameters and classes.**
  - Wrapper-made parameters: VST3 bypass `0x62797073` (`'byps'`) when the plugin
    has none and the program parameter `'prst'` `0x70727374` (`VST3.cpp:411-416,
    676-713, 727-755`); AAX always `"MasterBypassID"`, mapped to the plugin's
    bypass if it has one (`AAX.cpp:1728-1747`). JUCE's MIDI CC parameters
    (`0x6d636d00 + ch*130 + cc`, `VST3.cpp:1258-1267, 1766-1790`) are harmless to
    skip.
  - Class identity overrides on `PluginDescription`: the VST3 component id
    `FUID(0xABCDEF01, 0x9182FAEB, mfr, code)` (`juce_VST3Interface.h:181-250`), or
    under `CAN_REPLACE_VST2` `vst2PluginId(code, name)` (`'V' 'S' 'T'`, the code
    big-endian, up to 9 lowercased name characters, zero-padded; on Windows bytes
    0↔3, 1↔2, 4↔5 and 6↔7 swapped; `:74-115`); the AAX native id from a `'jcaa'`
    base, letters 3 and 4 advanced through `a-zA-Z0-9` by the input and output
    channel-set index (Mono→Mono `'jcbb'`, Mono→Stereo `'jcbc'`, Stereo→Stereo
    `'jccc'`, None→Stereo `'jcac'`) through WP10.17's hook; an AAX effect id
    override (JUCE's `JucePlugin_AAXIdentifier`, the bundle id by default); an
    `auType` override.
- **WP10.21, state: read JUCE's framing, write our own.** Writing JUCE's envelope
  gains nothing, since an old JUCE build cannot parse the JSON inside it.
  - *The bytes.* `copyXmlToBinary` is `"VC2!"` (`0x21324356` LE), a `u32` LE XML
    length N, N bytes of single-line UTF-8 XML and a `\0`; the reader ignores
    trailing bytes (`juce_AudioProcessor.cpp:946-980`). APVTS: the root's type is
    the plugin's identifier, every `PARAM id= value=` a direct child (groups
    flattened), values **plain** (a choice's index, a bool's 0 or 1, an int, a
    float in its units), a missing parameter keeping its value
    (`juce_AudioProcessorValueTreeState.cpp:117-169, 389-436`). A binary
    `ValueTree` and JUCE's non-RFC base64 (`"<size>.<chars>"`, alphabet
    `.A-Za-z0-9+`, LSB first, `juce_MemoryBlock.cpp:366-385`) appear too.
  - *VST3*: the component state is the plugin's bytes plus a trailer `[i64 0]
    [private ValueTree?] [i64 privSize] ["JUCEPrivateData"]`, the private tree
    `JUCEPrivateData {Bypass}` present only when the plugin has no bypass parameter
    (`VST3.cpp:2857-2943`); under `CAN_REPLACE_VST2` it is wrapped big-endian in
    `'VstW' size version bypass` and `'CcnK' size 'FBCh' ver fxID fxVersion
    numPrograms currentProgram 124×0 chunkSize chunk` (`VST3.cpp:3065-3082`, the
    SDK's `vst2persistence.cpp`). JUCE does nothing with `kStateType`.
  - *AU v2*: the SDK's ClassInfo dictionary plus `jucePluginState` (`CFData`);
    JUCE's `RestoreState` removes `data` before the base call so foreign ids never
    enter `Globals()`, and maps `version` 1 to 0 (`AU_1.mm:831-923`). Here the base
    call would insert JUCE's ids into map-mode `Globals()`, breaking the rule that
    it never grows.
  - *AUv3*: the base dictionary plus `jucePluginState`, no separate document state
    (`AUv3.mm:256-323`).
  - *AAX*: the SDK's `'elck'` controls chunk (normalized values by AAX id string)
    plus a `'juce'` chunk of raw state (`AAX.cpp:104-108, 794, 851-924`); MakeASound
    refuses any other chunk id.
  - *Standalone*: `~/Library/Application Support/<Name>.settings`, a
    `PropertiesFile` whose `filterState` is the JUCE-base64 state
    (`juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h:365-386`).
- **WP10.22, the foreign-state hook and its helpers.** SDK-free, in
  `Core/Plugin.h`: `enum class ForeignOrigin { JuceVST3, JuceVST2Bank, JuceAU,
  JuceAAX, JuceStandalone }`, `struct ForeignState { origin; std::string_view
  bytes; StateContext context; std::optional<bool> bypass; std::optional<int>
  program; }` and `virtual bool loadForeignState(const ForeignState&) { return
  false; }` (message thread); `PluginWrapper::loadForeignState` mirroring
  `loadState`'s deferral, stamp and `weak_ptr`; `StatePlugin` forwarding to
  `loadLegacy` and ending in `markChanged()`. Pure helpers in
  `Compat/JuceState.{h,cpp}` (`isJuceXmlBlob`, `juceXmlText`, `stripJucePrivateData`,
  `readVst2Bank` with `FxBk`, `fromJuceBase64`, `juceStringHash`) and
  `Compat/ApvtsXml.{h,cpp}` (a ~150-line XML subset reader; `loadApvtsXml(xml,
  params, mapId, MissingPolicy {Default, Keep})` setting plain values;
  `loadJuceApvtsBlob`). Detection is safe, since a MakeASound document starts with
  `{`.
- **WP10.23, the adapters.** VST3 `setState`: native if `{`, otherwise the bank,
  the trailer stripped, the hook. AU `RestoreState`, when `MakeASoundState` is
  absent: copy the dictionary, remove `data`, normalise the version, the base, the
  hook, mirror to the host. AUv3 `setFullState` reads `jucePluginState`. AAX
  accepts `'juce'` in `SetChunk`, answers `'elck'` and `'juce'` equal in
  `CompareActiveChunk`, and optionally reads `'elck'` for a plugin of parameters
  only. The standalone imports the old settings file when `settings.json` is
  missing. A session moves to the native JSON on its first save. Later, if wanted:
  a legacy XML written beside the native document, for downgrade safety.
- **Open:** whether Pro Tools calls `SetChunk` for a chunk id the plugin does not
  list in `GetChunkIDFromIndex`; if not, list `'juce'` with size 0. A check in Pro
  Tools Developer decides it.

### Done when

- A to C are in, each with its cases in the format suites (`VST3/`, `AU/`, `AUv3/`,
  `AAX/`, `Parameters/`, `State/`, `PluginUI/`, `Web/`) and the allocation suites
  covering every new audio-thread path (the VST3 output points, the AU moved bits,
  the wrapper's bypass delay).
- pluginval 10, `auval -strict` and aaxval stay green on both examples.
- By hand: a plugin-side change moves a host lane in Logic (AU), Ableton (VST3) and
  Pro Tools (AAX); a session saved by the AUv3 on an iPad loads in the v2 component
  on a Mac; Gain without its bypass parameter bypasses from each host's button.
- E, when it is taken up, has a JUCE build of one example and the MakeASound build
  of it opening each other's sessions in every format the JUCE build shipped.
- CLAUDE.md, README and this plan describe it as it is, CLAUDE.md's AAX
  native-id sentence included.

### Open decisions

1. **The default hash**: keep FNV-1a over the id path (recommended, WP10.8), or
   JUCE's leaf-id hash.
2. **Hosting**: the hosting work lives in Plug's local branch `hosting-gaps`
   (`~/Code/HostingGapsWorkspace`, `Lib/TamberPlug/VST3/Host/HostedParameter.h`),
   whose `HostedParameter` overrides accessors that are non-virtual in
   MakeASound's `Parameter` (`Parameters/Parameter.h` ~50-57). If hosting moves
   into MakeASound, choose virtual accessors or a separate interface before
   `ParameterOptions` grows.

## Later stages, in brief

- **CLAP**: the ids and the per-event MIDI path are already prepared; note ids and
  per-note expression are the remaining `MIDI::Event` gap.
- **AUv3 on macOS**: the adapter already builds and is tested there; shipping it
  would need a macOS container app and a decision on coexisting with the v2
  `.component`.
- **AAX on Windows** (MSVC x64; clap-wrapper and Pro Tools rule out ARM64 and
  clang-cl), with a `View-Windows.cpp`; then surround and side-chain stems
  (`AddSideChainIn` from a non-main input bus) once a bus carries channel roles,
  and AudioSuite.
- **MIDI 2.0**: `MIDIEventList`/UMP in AU and AUv3, which take MIDI 1.0 bytes only.
- **Editors**: persisting an editor's size (in the standalone's `Settings` or a
  format's state), a host-driven content scale beyond what `PlugView` forwards,
  and a generic React parameter page shipped by the library.

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

- 2026-10-11: an editor states a size policy, `EditorConstraints {resizable,
  minimum, maximum, aspectRatio}`, and one function, `constrain`, applies it in
  every format; `Editor::isResizable()` is gone with no shim. An editor asks for a
  size through `requestResize`, which its `EditorHost` (the standalone window, the
  VST3 `PlugView`, the AU view) answers; a host whose window has no grip, the AU
  view, gets a `ResizeGrip` drawn by the editor.
- 2026-10-11: the AU view follows Plug's heuristics for Logic's view service: it
  resizes a same-size container with itself, never a window's content view, and
  on opening moves its window on screen over up to 12 attempts, then shrinks
  once keeping its proportions. The geometry lives in `ViewPlacement`, pure and
  tested without a window.
- 2026-10-11: AU bypass is `kAudioUnitProperty_BypassEffect` over the plugin's one
  exposed bypass parameter (`ParameterList::bypassIndex()`), written through the
  parameter path so a held gesture drops it; a plugin with none refuses the
  property. VST3 flags only that entry `kIsBypass`.
- 2026-10-11: a web editor is a library, `MakeASoundPluginWeb`, and a CMake
  function of its own, `makeasound_add_web_editor`, rather than a `WEB_DIR` on
  `makeasound_add_plugin`; the page is embedded into the plugin's static core
  through ResEmbed, so every format carries it. Lists and ids cross the bridge
  inside structs, because Miro's TypeScript codegen cannot type a bare `Vector`
  or string argument. The Synth example carries the page; Gain keeps the generic
  editor.
- 2026-10-11: CI sets up Node 22 on every desktop job and the iOS one, because the
  Synth's page runs `npm install` at configure and vite in the build wherever eacp
  has a web runtime, iOS included; Linux builds the fallback editor.
- 2026-10-12: AAX is fetched, never vendored: `MAKEASOUND_BUILD_AAX` (off by
  default) has CPM fetch `github.com/audiosdk/aax`, a public GPLv3-option fork of
  SDK 2.8.1 with only deletions on top, pinned to commit `faf5d8e`, with
  `CPM_AAXSDK_SOURCE` for a local copy. The repository stays MIT and a default
  build compiles no Avid code; an AAX binary is GPLv3 unless its author holds
  Avid's agreement. JUCE vendors the SDK on the same GPLv3 option; vendoring
  would have put a GPL component in an MIT tree. The validator and developer
  tools are NDA-confidential and only ever run from the user's path.
- 2026-10-12: AAX is one monolithic native `AAX_CEffectParameters` per plugin,
  the algorithm a member call reached through a private-data field, as in JUCE
  and clap-wrapper; one component per accepted Mono/Stereo stem pair, native id
  `pluginCode + stem index` (MakeASound's own derivation, not JUCE's, which
  WP10.17 and WP10.20 address), parameter ids the 31-bit host id in hex. Host
  parameter writes go straight into the wrapper, so there is no packet dispatcher.
- 2026-10-12: the AAX module includes the SDK's `AAX_Init.cpp` with `AAXShutdown`
  renamed and defines its own, which releases eacp's message thread before the
  SDK's shutdown, because Pro Tools unloads the module right after it; nothing of
  eacp's may outlive the code it calls into, as pluginval's cold reload showed for
  VST3 on Windows.
- 2026-10-12: an AAX Native block is at most 1024 samples, the SDK's own ceiling;
  the adapter prepares for that and renders a larger host block in slices of it,
  MIDI and playhead per slice, rather than trusting the host never to exceed it.
- 2026-10-12: an AAX component gives every context field a port, a placeholder
  private-data block where a MIDI node is not wanted, because a gap in the
  algorithm context fails the description; the field order of `AlgorithmContext`
  is ABI and only ever appended to.
- 2026-10-12: AAX parameter values flow one way each: a host write goes into the
  wrapper and the `AAX_CParameter` keeps the host's value as written, and the
  timer mirrors to the host only what the plugin moved itself on parameters no
  gesture holds, so a value the plugin snaps is never written back over the host's.
- 2026-10-12: aaxval is driven over DigiShell's stdin, one `runtest` per listed
  test rather than `runtests`, so each test gets its own verdict and an
  inapplicable one (`E_CANCELED`) is told apart from a failure;
  `test.cycle_counts` and `test.page_table.load`, which fail outright for a native
  plugin with no page table, are skipped by id with their reasons in the log. The
  validator is the user's NDA copy, so nothing runs it but `PluginValidator`, and
  never two at once.
- 2026-10-12: the AAX view pins eacp's surface to the container's top-left with
  an autoresizing mask rather than filling the container, since Pro Tools sizes
  the container after asking `GetViewSize`; a top-left option on eacp's
  `EmbeddedView` would replace it.
- 2026-10-12: CI builds the AAX format on the macOS job only and verifies the
  bundles' slices, exports, `PkgInfo`, plist and signature, but neither validates
  nor signs them; installing into Avid's system folder stays best effort there,
  as everywhere.
- 2026-10-12: AUv3 ships on iOS only; macOS keeps the v2 `.component` as its AU.
- 2026-10-12: no shared framework for AUv3. The static plugin core links into both
  the iOS app and its appex, each an executable. In a framework eacp's `isDLL()`
  is true, so `Apps::run` never starts `UIApplicationMain` and the app exits; two
  eacp call sites reach `[UIApplication sharedApplication]`, which an extension
  may not; and a dylib's exports defeat LTO (eacp-core 479 KB as a framework, 107
  KB static). JUCE ships AUv3 the same way. The eacp change that would allow a
  framework, `isDLL()` false on iOS, is a follow-up for eacp.
- 2026-10-12: the AUv3 adapter is one C++ class, `AUv3::Adapter`, in the static
  `MakeASoundAUv3`; each module compiles a thin `AUAudioUnit` subclass over it,
  named with `MAKEASOUND_AUV3_CLASS_SUFFIX`, so no Objective-C class lives in an
  archive and two extensions in one process never share one. The render block
  captures the adapter's C++ pointer and calls the host's blocks through
  unretained copies of strong references taken at allocate, so no retain, release
  or message runs on the audio thread. Null output data gets the adapter's own
  buffers, and a block past `maximumFramesToRender` is refused there, since the
  base class checks neither.
- 2026-10-12: an `AUParameter`'s identifier is the parameter's id path and its
  address the 31-bit host id: the v2 bridge hashes the identifier into the
  `AudioUnitParameterID` a v2 host saves, so it must be as stable as the id. Each
  named group is an `AUParameterGroup` identified by its id path, which needed the
  enclosing groups' names on `ParameterList::Entry` (`groups`).
- 2026-10-12: the AUv3 plist for an iOS build is written by a host tool linking the
  plugin's core built a second time for macOS, in an `ExternalProject` sub-build:
  an iOS binary can be neither `dlopen`ed nor run on the build machine, and the
  component type needs the plugin constructed to read its layout.
  The adapter still builds and is tested on macOS, where the allocation tests run.
- 2026-10-12: the iOS AUv3 ships inside the plugin's full iOS standalone, not a
  placeholder container.
- 2026-10-12: the standalone's UI stays eacp-ui widgets on every platform, iOS
  included; no WebView enters it. What is not UI moves into `StandaloneHost`, and
  a plugin may replace the app entirely (`STANDALONE_SOURCES`) while keeping the
  packaging.
- 2026-10-12: `StandaloneHost` is constructed and started in two steps: the
  constructor loads state, `start()` opens ports and the stream, and a save before
  `start()` keeps the file's stream and ports, so an app builds its UI first and
  never overwrites the user's devices with nothing.
- 2026-10-12: the default standalone is one class with a per-platform `Shell`
  (`StandaloneApp-Desktop.cpp`, `StandaloneApp-iOS.cpp`), not a second app class
  for iOS: the editor creation, the settings wiring and the reset are shared, and
  the platform split is by TU, as everywhere else.
- 2026-10-12: the microphone string is a caller's flag, `NO_AUDIO_INPUT`, since a
  plugin's layout is only known at run time and the plist is written at build
  time.
- 2026-10-12: a custom app still links `MakeASoundStandalone`, so its pieces are
  there to take and the default app's unreferenced objects stay out of the link;
  the packaging is `makeasound_add_plugin`'s either way.
- 2026-10-12: the Synth example's editor drops its 3:2 lock (360×240 to
  1200×800, any ratio): on a phone the screen's shape is the device's, a locked
  editor leaves most of a portrait screen empty, and the page reflows instead.
- 2026-10-12: AUv3 packaging works under both Ninja (bundles laid out by hand,
  as the `.component` is) and the Xcode generator (embedding and automatic
  signing, for signed device builds); CI builds both.
- 2026-10-12: the AUv3 view is one view controller class per platform TU, compiled
  into each module under its suffix, over a shared C++ `EmbeddedEditor` in a
  static target of its own (`MakeASoundAUv3View`), so `MakeASoundAUv3` links no
  UI tier. It asks for a size through `preferredContentSize` and answers
  `supportedViewConfigurations:` by whether the editor's smallest size fits.
- 2026-10-12: the AUv3 plist tool's host build is an `ExternalProject` over the
  top-level project itself (ours or a consumer's) with `MAKEASOUND_DESCRIBE_BUILD`
  on and every CPM package pointed at the iOS tree's sources, not a generated
  project: only the project knows how a plugin's core is built.
- 2026-10-12: an AUv3 extension's `CFBundleVersion` is `VERSION`, its app's, rather
  than `ModuleDescription::version`: an extension must match its container. The
  components' `version` integers still come from `PluginDescription::version`.
- 2026-10-12: a Ninja iOS device build is unsigned, app and extension alike; the
  simulator is signed ad hoc, and a signed device build is Xcode's, through
  `MAKEASOUND_IOS_TEAM`.
