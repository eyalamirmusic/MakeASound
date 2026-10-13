# CLAUDE.md

Guidance for Claude Code in this repository. It is deliberately short: it lists what you need every session and the traps the code doesn't make obvious. For detail, read the code, `README.md`, `plan.md` (plugin stages) and `gaps.md`. Comments in the headers are the reference.

## Project

MakeASound is a C++20 library for platform-agnostic audio, MIDI and audio plugins.

- **`MakeASound`** (`Lib/MakeASound/`): `DeviceManager` (audio, backed by miniaudio) and `MidiManager` (Core MIDI, WinMM or ALSA sequencer, chosen per platform). Each façade hides its backend behind a pimpl, and no backend type reaches a public header.
- **`MakeASoundDSP`** (`DSP/`): an optional DSP library. Today it holds `TestSynth`.
- **`MakeASoundPlugin`** (`Plugin/`): the SDK-free plugin core: `Plugin`, `StatePlugin`, parameters, state, `PluginWrapper` (the per-block pipeline every format shares) and `Compat/` (readers for state that JUCE builds saved).
- **Formats on top:** Standalone (`StandaloneCore` with no window, plus `Standalone` with windows), VST3 (vendored SDK), AU v2 (macOS, fetched AudioUnitSDK), AUv3 (Apple, no SDK), AAX (macOS, opt-in through `MAKEASOUND_BUILD_AAX`). Each one is a static target that exists only where its dependency target exists. A format whose target is missing is skipped with a status line.
- **UI:** `MakeASoundPluginUI` (eacp-ui: `GenericEditor`, `ResizeGrip`) and `MakeASoundPluginWeb` (React editors over eacp's `WebView`).
- **Tools:** `PluginValidator` (pluginval, auval and the user's own aaxval) and `AUv3Probe` (iOS simulator).

Public umbrellas: `<MakeASound/MakeASound.h>` and `<MakeASound/Plugin/MakeASoundPlugin.h>`. The include root is `Lib/`.

## Build

CPM fetches every dependency at configure time. Linux also needs `libasound2-dev`, plus the eacp graphics and libcurl dev packages; see the apt step in `.github/workflows/ci.yml`.

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/Tools/PluginValidator/PluginValidator build/VST3   # also build/AU
```

On Windows ARM64, build from a VS dev shell (see memory).

- Options: `MAKEASOUND_BUILD_PLUGIN`, `_BUILD_HOSTING` (needs `_BUILD_PLUGIN`; macOS and Windows for now), `_BUILD_APPS`, `_BUILD_EXAMPLES` (the plugin examples need Node/npm at configure time), `_BUILD_AAX`, `_UNITY_BUILD`, `_CI_BUILD`, `_INSTALL_PLUGINS`, `_IOS_TEAM`.
- `CMAKE_CXX_SCAN_FOR_MODULES` is OFF on purpose. A scanned source falls out of its unity chunk, which silently un-unities MSVC and clang-cl builds.
- `-DCPM_<Name>_SOURCE=/path` points a dependency at a local checkout.
- Plugins are declared with `makeasound_add_plugin(<Name> FORMATS ... SOURCES ...)` (`CMake/MakeASoundPlugin.cmake`) and web editors with `makeasound_add_web_editor` (`CMake/MakeASoundWebEditor.cmake`). Read those files before changing packaging, bundle layout or signing.
- Examples live in `Plugins/Gain`, `Plugins/GainApp` and `Plugins/Synth`, and demo apps in `Apps/`.

## Conventions

- C++20, namespace `MakeASound` (backends and formats in sub-namespaces). Anything that links `ausdk` is the one exception: it is C++23.
- Always use `auto` for variables. Use RAII. Everything that does not allocate is `noexcept`. Sizes and indices are `int`.
- Platform-specific code goes in per-platform TUs (`Name-macOS.cpp`, `-iOS.mm`, `-Windows.cpp`, `-Linux.cpp`, `-Default.cpp`) selected in CMake. Never use `#if` in a shared TU or header.
- `.clang-format`: Allman, 4 spaces, 85 columns. **Never reorder includes**, since the order is intentional.
- To add a library file, create `Lib/MakeASound/<Folder>/<Name>.{h,cpp}` and list it in that target's `CMakeLists.txt`. Sources are listed individually. Siblings are included as `"Name.h"`, other folders as `"../Folder/Name.h"`.
- IDE folders are set with `CMAKE_FOLDER`. `CMake/ExternalFolders.cmake` files every dependency target under `External/`.
- **Git:** never commit or push without explicit confirmation in the conversation. Feature branches come off `develop` and their PRs go into `develop`. Only `develop` goes into `main`.

## Invariants: don't break these

**Device library**
- Neither the audio path nor the MIDI path throws. Errors come back as `Error`, and backends map native codes through `getError`. Recovery runs on its own thread, where an exception has nowhere to go.
- Every notification takes two paths: `notificationCallback` (immediate) and the queue drained by `drainNotifications()`. Keep both fed from `notifyHost`.
- `DeviceManager::openStream` wraps the user callback to set `AudioCallbackInfo::dirty`, because the raw backend never sets it.
- `lastCallbackUs` stays in microseconds.
- Device and MIDI port ids are registry slots keyed by stable identity, not enumeration positions.
- miniaudio is initialised with `ma_ios_session_category_none` and `noAudioSessionActivate`. The iOS session is ours, and preserving this matters when editing `initContext`.
- There is no `getDefaultConfig()`. A direction-explicit default exists because an unwanted capture side costs a microphone permission.
- Core MIDI clients live on the process-wide `NotifyLoop` thread. ALSA sends use `snd_seq_event_output` and drain, never `_direct`, which allocates. In WinMM, `drainMessages()` is the audio thread's, so nothing that notifies runs there.

**Plugin core**
- `MakeASoundPlugin` includes no eacp header and no format SDK. eacp is confined to the `Realtime/MessageThread*.cpp` TUs, and `Editor.h` only forward-declares the view.
- Adapters drive `PluginWrapper` in its documented order and never call the plugin's `process` themselves. The wrapper is destroyed on the message thread.
- Audio-thread code is allocation-free. A new realtime path gets a case in the allocation suites: `AllocationTests.cpp` (device library only; it must build without the plugin core), `PluginAllocationTests.cpp`, or the per-format `*AllocationTests`.
- Parameter state keys and host ids derive from the id *path*. Pin `id`/`hostId` before regrouping, or every session and automation lane breaks.
- Identity is an `IdScheme` profile resolved into an `IdentityPolicy` (`Core/IdentityPolicy.h`). Adapters and `ParameterList` read only the policy's rules, never the profile: a new behaviour is a new rule, not another `isJuceScheme()` check. Index profiles (`JUCELegacyIndex`, `TamberPlug`) count every leaf, hidden ones included; a parameter with a hint past `indexedThroughVersion` is post-port and must move no index.
- `Tests/Identity/*.txt` are golden manifests of every id a host stores for each test plugin. A diff there is a change to a shipped identity: make it on purpose, regenerate with `MAKEASOUND_UPDATE_IDENTITY=1 ./build/Tests/MakeASoundTests --test Identity/<case>`, and review it. `identityProblems()` must stay empty for every test plugin (CI builds Release, where nothing asserts).
- Shipped identities never change: the VST3 class id derivation, AU codes, the AAX ids and the stem-pair order (`stemPairs()` is append-only).
- `ScopedMemoryAllocations` / `AllocationsChecker` is test-only. It must never reach `Lib/` or `Apps/`.

**Formats**
- Every VST3 TU includes `VST3Common.h` first, because of the `setState`/`getState` rename in `vstsinglecomponenteffect.h`.
- Objective-C classes are named per module (`MAKEASOUND_AU_VIEW_CLASS`, `MAKEASOUND_AUV3_CLASS`), because the ObjC runtime is shared by the whole process.
- AU and AAX bundles are laid out by hand, not as CMake `BUNDLE` targets, because CMake rewrites the plist on every configure.
- The AU `CFBundleVersion` comes from `ModuleDescription::version`, because macOS keys its component cache on it.
- Don't release the host message thread from an `__attribute__((destructor))`: auval aborts at exit.
- AUv3 on iOS configures the tree a second time for macOS (`MakeASoundDescribeBuild`) to write plists, so plugin sources must also build for macOS.
- Tests reach the AU v2 adapter only through the AudioUnit C API. Components are registered with `AudioComponentRegister`, never found through `AudioComponentFindNext`. The one approved exception is the hosting suites: scanning an Audio Unit *is* a registry lookup, so they scan Apple's always-present `aufx:dely:appl` and `aumu:dls :appl`, and a hosting test that loads an AU fixture registers it and takes the registered `AudioComponent` directly.
- AAX: the SDK is GPLv3/Avid-licensed and fetched only under the flag. The AAX Validator, DigiShell and Avid tools are under NDA. Run them only from a path the user names; never fetch, commit or run them in CI. Only one `dsh` may run at a time, since it uses a fixed port.
- Web editors: `src/generated` and `package-lock.json` are committed; `node_modules`, `dist` and `.vite` are not.
- `ThirdParty/VST3_SDK` is vendored and trimmed (see `ThirdParty/README.md`).

## Tests

NanoTest, one executable (`MakeASoundTests`). Suites are gated on which targets exist (see `Tests/CMakeLists.txt`). Allocation suites run only on Apple and Linux, because they need `dlsym(RTLD_NEXT)`. Shared fixtures:

- `Tests/TestPlugins.h`
- `Tests/JuceFixtures.h` (bytes written by JUCE 8.0.12)
- `Tests/TamberPlugFixtures.h` (ClassInfo written by Tamber Plug's AU, what Plug's AU and VST3 published, the ports)
- `Tests/JucePort.h` (with what JUCE 8.0.12's own builds of its twin published)
- `Tests/IdentityManifest.h` (`describeIdentity`, against `Tests/Identity/`)
- `Tests/*TestHost.h`, one per format
- `Tests/HostingFixtures.h` (a plugin database written by Tamber Plug's scanner), `Tests/HostingTestFormats.h` (stub formats), `Tests/FakeScanWorker.cpp` (a worker that misbehaves on request) and `Tests/HostFixtures/` (VST3 fixture plugins, never installed, built into `<build>/HostFixtures`). Hosting tests use these and Apple's built-in AUs only, never a third-party plugin.

`PluginTests.cpp` defines the tests' `describeModule()`. Cases that write files use their own temp folder, because ctest runs cases in parallel processes.
