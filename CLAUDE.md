# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

MakeASound is a C++20 static library that gives platform-agnostic access to audio and MIDI. Two façades make up the public surface:

- `MakeASound::DeviceManager` — audio device enumeration and streaming, backed by [miniaudio](https://github.com/mackron/miniaudio).
- `MakeASound::MidiManager` — MIDI input/output ports, backed by [RtMidi](https://github.com/thestk/rtmidi).

Each façade hides its backend behind a pimpl, so no backend types leak into the public headers. Today there is one backend per façade, but the pimpl seam is the place to add others.

## Build

Dependencies are fetched automatically by [CPM.cmake](CMake/CPM.cmake) the first time CMake configures — no manual install. The library pulls in miniaudio, RtMidi, [Miro](https://github.com/eyalamirmusic/Miro) (reflection/serialization), and `ea_data_structures`; the demo apps additionally pull in `eacp` (web-UI host). Their transitive deps (e.g. ResEmbed, NanoTest) come along automatically.

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/Apps/Example/Example     # plain CLI: streams white noise for 2s
./build/Apps/MidiDemo/MidiDemo   # plain CLI: MIDI demo
```

`Apps/Demo` and `Apps/Synth` build macOS `.app` bundles with web (React/Vite) UIs hosted via `eacp`/Miro's webview; their web assets are bundled at build time.

To point CPM at a local checkout of a dependency instead of fetching, pass e.g. `-DCPM_RTMidi_SOURCE=/path/to/rtmidi` at configure time.

CMake options: `MAKEASOUND_UNITY_BUILD` (jumbo build, OFF) and `MAKEASOUND_BUILD_APPS` (ON, top-level builds only).

CI lives in `.github/workflows/ci.yml` and builds on macOS (universal arm64+x86_64), Windows (MSVC and clang-cl), and Linux (GCC).

The repo ships a CLion-generated `cmake-build-debug/` directory; use it or create a fresh build dir as you prefer. `.clang-format` and `.clang-tidy` are present but invoked manually or via the IDE — there is no lint target.

## Architecture

Public surface: `Lib/MakeASound/MakeASound.h` is the sole umbrella public header. Downstream code does `#include <MakeASound/MakeASound.h>`. The include root is `Lib/` (so the umbrella resolves as `<MakeASound/MakeASound.h>`); cross-module headers inside the library include each other by relative path.

Code under `Lib/MakeASound/` is grouped into domain folders, each compiled as separate TUs:

- **`Audio/`** — `Buffer` and `Channel`, non-owning views over a callback's audio block. The block is **planar** (channel-major): `Buffer` holds the flat `std::span<float>` plus the channel count and slices it into per-channel `Channel`s; `Buffer::channels()` is a non-allocating range for `for (auto channel : buffer.channels())`. `Channel` is the single-channel currency used everywhere in place of a bare `std::span<float>` — it is a contiguous range, so range-for and the standard algorithms work on it directly. Both are header + `.cpp`, all methods `noexcept`.

- **`Devices/`** — plain-data types and the audio façade.
  - `DeviceInfo.{h,cpp}` holds backend-independent structs/enums (`DeviceInfo`, `StreamConfig`, `StreamParameters`, `StreamOptions`, `Flags`, `AudioCallbackInfo`, `Error`, `AudioCallbackStatus`, `Callback`). No backend types leak in. `AudioCallbackInfo` exposes the block via `getOutput()` / `getInput()` (returning `Buffer`) and carries a `dirty` flag, raised when the stream shape (channels, sample rate, block size) changes since the previous callback — the signal for consumers to (re)allocate working buffers. Data structs opt into Miro JSON reflection in-place via `MIRO_REFLECT(...)`.
  - `DeviceManager.{h,cpp}` is the audio façade. It holds an `OwningPointer<MiniAudio::DeviceManager>` pimpl; the header forward-declares the backend type and the destructor is defined out-of-line in `.cpp` so the deleter sees the complete type. The `.cpp` is the only place that constructs the backend (`EA::makeOwned<MiniAudio::DeviceManager>()`).
  - `BlockSizes.{h,cpp,mm}` queries a device's supported block sizes; the `.mm` is the CoreAudio implementation on macOS, with a conservative fallback elsewhere.

- **`MIDI/`** — MIDI types and the MIDI façade: `MIDI.{h,cpp}` (typed events + raw-byte conversion), `MidiInfo.{h,cpp}` (port info, event/message wrappers), `MidiBlockSync.{h,cpp}` (aligns incoming MIDI to audio block boundaries), and `MidiManager.{h,cpp}` (façade with an `OwningPointer<RTMidi::MidiManager>` pimpl, same out-of-line-destructor pattern).

- **`UI/`** — optional helpers (`Dropdown`, `UIDeviceManager`, `UIMidiManager`) that wire device/MIDI state to Miro UI widgets used by the demo apps.

- **`Common/`** — `Common.h` re-exports `EA::Vector`/`Array`/`OwningPointer` into the `MakeASound` namespace; `Algorithms.h` has small audio-thread-safe helpers.

- **`MiniAudio/`** and **`RTMidi/`** — the external-library backends (namespaces `MakeASound::MiniAudio` and `MakeASound::RTMidi`). Each has a `*-Backend.{h,cpp}` of pure conversion functions and a `*Manager.{h,cpp}` that owns the library handle. `MiniAudio::DeviceManager` owns the `ma_device` and the `audioCallback` trampoline that miniaudio invokes; it **de-interleaves** the incoming/outgoing interleaved buffers into the planar scratch the callback sees and re-interleaves on the way out (so callers only ever see planar data). `RTMidi::MidiManager` owns the `RtMidiIn`/`RtMidiOut` ports.

**Adding a new file/module:** create `Lib/MakeASound/<Folder>/<Name>.{h,cpp}` (reuse an existing domain folder where it fits) and add `MakeASound/<Folder>/<Name>.cpp` to the source list in `Lib/CMakeLists.txt`. Include siblings in the same folder as `"<Name>.h"` and headers in other folders as `"../<Folder>/<Name>.h"`.

**Dirty-flag flow:** `DeviceManager::openStream` wraps the user callback in a lambda that compares the incoming `AudioCallbackInfo` against `prevInfo` (via the struct's `operator==` on channels/sampleRate/maxBlockSize) and sets `info.dirty = true` on change. Preserve this wrapping when editing `openStream` — the raw backend callback does not set `dirty`.

**Serialization:** Miro provides reflection natively for primitives, integrals, `std::vector`/`std::array`/`std::map`/`std::optional`, and enums (string-named via `Miro::enumToString`). The data structs use `MIRO_REFLECT(...)` directly, so anything that includes the public header transitively pulls in Miro.

## Conventions

- C++20, namespace `MakeASound`; backend code in `MakeASound::MiniAudio` and `MakeASound::RTMidi`.
- `.clang-format`: Allman braces, 4-space indent, 85-col limit, pointer-left, no tabs, `NamespaceIndentation: None`, `SortIncludes: false` (include order is intentional — don't reorder).
- The `MakeASound` CMake target is a `STATIC` library; sources are listed individually in `Lib/CMakeLists.txt`. It links `Miro` PUBLIC (Miro headers leak through the public data structs) and `miniaudio` + `rtmidi` PRIVATE (both backends are fully hidden behind the façades).
- always use auto for variables
- use modern RAII code whenever possible
