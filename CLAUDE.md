# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

MakeASound is a C++20 static library wrapping [RtAudio](https://github.com/thestk/rtaudio). The public `MakeASound::DeviceManager` façade gives platform-agnostic access to audio devices and streaming, while a backend implementation (currently only RtAudio) lives behind it.

## Build

Dependencies (RtAudio, [Miro](https://github.com/eyalamirmusic/Miro)) are fetched automatically by [CPM.cmake](CMake/CPM.cmake) the first time CMake configures. No manual install is needed.

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/Example       # runs Example/Example.cpp
```

To point CPM at a local checkout of a dependency instead of fetching, pass e.g. `-DCPM_RTAudio_SOURCE=/path/to/rtaudio` at configure time.

The repo already ships a CLion-generated `cmake-build-debug/` directory; use it or create a fresh build dir as you prefer.

There is no test target and no lint/CI script wired up — `.clang-format` and `.clang-tidy` are present but invoked manually or via the IDE.

## Architecture

Public surface: `Lib/MakeASound/MakeASound.h` is the sole umbrella public header. Downstream code does `#include <MakeASound/MakeASound.h>`.

Three internal modules, each in its own subdirectory and compiled as a separate TU:

1. **Plain-data types** (`Lib/MakeASound/DeviceInfo/DeviceInfo.h`) — backend-independent structs/enums (`DeviceInfo`, `StreamConfig`, `StreamParameters`, `Format`, `Flags`, `AudioCallbackInfo`, `Callback`). Header-only (no `.cpp`). No RtAudio types leak into this header. `AudioCallbackInfo` carries buffers plus a `dirty` flag that is raised when the stream shape (channels, sample rate, block size) changes since the previous callback — consumers use it as the signal to (re)allocate working buffers. The data structs use `MIRO_REFLECT(...)` in-place to opt into Miro's JSON reflection.

2. **Public façade** (`Lib/MakeASound/DeviceManager/DeviceManager.h` + `.cpp`) — `MakeASound::DeviceManager` uses a `std::unique_ptr<RTAudio::DeviceManager>` pimpl. The header forward-declares `RTAudio::DeviceManager`; the destructor is defined out-of-line in `.cpp` so unique_ptr's deleter sees the complete type there. The `.cpp` is the only place that constructs the backend — to add a second backend, change the `make_unique` and forward-declared type.

3. **RtAudio backend** (`Lib/MakeASound/RTAudio/`) — `RTAudio-Backend.{h,cpp}` holds pure conversion functions between MakeASound and RtAudio types (`getFormat`, `getInfo`, `getStreamParams`, `getCallbackInfo`, etc.); the templates `bitCompare` and `optionalToPointer` stay in the header. `RTAudioDeviceManager.{h,cpp}` owns the `RtAudio` instance and the static `audioCallback` trampoline that RtAudio invokes, which repacks arguments into `AudioCallbackInfo` and forwards to the user callback.

**Adding a new module:** create `Lib/MakeASound/<Module>/<Module>.{h,cpp}` and add `MakeASound/<Module>/<Module>.cpp` to the source list in `Lib/CMakeLists.txt`. Cross-module headers include via relative paths like `"../DeviceInfo/DeviceInfo.h"`.

**Dirty-flag flow:** the façade wraps the user callback in a lambda that compares the incoming `AudioCallbackInfo` against `prevInfo` (via the struct's `operator==` on channels/sampleRate/maxBlockSize) and sets `info.dirty = true` on change. Preserve this wrapping when editing `DeviceManager::openStream` — the raw backend callback does not set `dirty`.

**Serialization:** Miro provides everything needed natively — built-in reflection for primitives, integrals, `std::vector`/`std::array`/`std::map`/`std::optional`, and enums (string-named via `Miro::enumToString`). The data structs in `DeviceInfo.h` use `MIRO_REFLECT(...)` directly, so anything that includes the public header transitively pulls in Miro.

## Conventions

- C++20, namespace `MakeASound` (backend code in `MakeASound::RTAudio`).
- `.clang-format`: Allman braces, 4-space indent, 85-col limit, pointer-left, no tabs, `NamespaceIndentation: None`, `SortIncludes: false` (include order is intentional — don't reorder).
- The `MakeASound` CMake target is a `STATIC` library; sources are listed individually in `Lib/CMakeLists.txt`. It links `Miro` PUBLIC (Miro headers leak through `DeviceInfo.h`) and `rtaudio` PRIVATE (RtAudio is fully hidden behind the façade).
- always use auto for variables
- use modern RAII code whenever possible
