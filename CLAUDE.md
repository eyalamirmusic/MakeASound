# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

MakeASound is a header-only C++20 wrapper around [RtAudio](https://github.com/thestk/rtaudio). The public `MakeASound::DeviceManager` façade gives platform-agnostic access to audio devices and streaming, while a backend implementation (currently only RtAudio) lives behind it.

## Build

Dependencies (RtAudio, nlohmann_json, magic_enum) are fetched automatically by [CPM.cmake](CMake/CPM.cmake) the first time CMake configures. No manual install is needed.

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/Example       # runs Example/Example.cpp
```

To point CPM at a local checkout of a dependency instead of fetching, pass e.g. `-DCPM_RTAudio_SOURCE=/path/to/rtaudio` at configure time.

The repo already ships a CLion-generated `cmake-build-debug/` directory; use it or create a fresh build dir as you prefer.

There is no test target and no lint/CI script wired up — `.clang-format` and `.clang-tidy` are present but invoked manually or via the IDE.

## Architecture

Three layers, all header-only:

1. **Plain-data types** (`Include/MakeASound/DeviceInfo.h`) — backend-independent structs/enums (`DeviceInfo`, `StreamConfig`, `StreamParameters`, `Format`, `Flags`, `AudioCallbackInfo`, `Callback`). No RtAudio types leak into this header. `AudioCallbackInfo` carries buffers plus a `dirty` flag that is raised when the stream shape (channels, sample rate, block size) changes since the previous callback — consumers use it as the signal to (re)allocate working buffers.

2. **Public façade** (`Include/MakeASound/DeviceManager.h` + `DeviceManagerImpl.h`) — `MakeASound::DeviceManager` uses a **type-erased pimpl via `std::any`** (holding a `std::shared_ptr<RTAudio::DeviceManager>`). This is why the implementation lives in `DeviceManagerImpl.h` (inline definitions) rather than a `.cpp`: the header-only contract would otherwise force every translation unit to see RtAudio. Clients that only need the types include `MakeASound.h` / `DeviceManager.h`; clients that actually construct a `DeviceManager` must include `DeviceManagerImpl.h` (see `Example/Example.cpp`). If a second backend is ever added, swap the `using RT = ...` alias and the `make_shared<RT>()` call in `DeviceManagerImpl.h`.

3. **RtAudio backend** (`Include/MakeASound/RTAudio/`) — `RTAudio-Backend.h` holds pure conversion functions between MakeASound and RtAudio types (`getFormat`, `getInfo`, `getStreamParams`, `getCallbackInfo`, etc.). `RTAudioDeviceManager.h` owns the `RtAudio` instance and the static `audioCallback` trampoline that RtAudio invokes, which repacks arguments into `AudioCallbackInfo` and forwards to the user callback.

**Dirty-flag flow:** the façade wraps the user callback in a lambda that compares the incoming `AudioCallbackInfo` against `prevInfo` (via the struct's `operator==` on channels/sampleRate/maxBlockSize) and sets `info.dirty = true` on change. Preserve this wrapping when editing `DeviceManager::openStream` — the raw backend callback does not set `dirty`.

**Serialization** (`Include/MakeASound/Serializing/Serializing.h`) is optional and lives behind its own include. It uses `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` to (de)serialize the public structs, plus generic `adl_serializer` specializations for any enum (via `magic_enum`) and for `std::optional<T>`. The core library does not depend on nlohmann_json or magic_enum — only the `Example` target links them, so code under `Include/MakeASound/` outside the `Serializing/` subdir must not include these headers.

## Conventions

- C++20, namespace `MakeASound` (backend code in `MakeASound::RTAudio`).
- `.clang-format`: Allman braces, 4-space indent, 85-col limit, pointer-left, no tabs, `NamespaceIndentation: None`, `SortIncludes: false` (include order is intentional — don't reorder).
- The `MakeASound` CMake target is `INTERFACE` (header-only); it links `rtaudio` transitively. Consumers that use `DeviceManagerImpl.h` inherit the RtAudio link automatically.
- always use auto for variables
- use modern RAII code whenever possible
