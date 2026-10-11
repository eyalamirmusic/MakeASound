# Third-party code kept in this tree

Everything else MakeASound depends on is fetched by CPM at configure time. What
lives here is vendored instead, so a checkout builds offline and the copy can be
trimmed to what the library uses.

## VST3_SDK

Steinberg's VST 3 SDK, version 3.8.1 (`v3.8.1_build_84`, commit `3cdf9ca`), MIT
licensed. The SDK's own `LICENSE.txt` and `README.md` sit at the top of the
folder and inside each of its sub-repositories (`base`, `pluginterfaces`,
`public.sdk`), and `VST3_Usage_Guidelines.pdf` is the trademark guide the README
refers to: the "VST" name and the VST Compatible logo are optional, and used
only on the terms in there.

`base` and `pluginterfaces` are complete. `public.sdk` is cut down to the plugin
side of a single-component effect: `source/common` (iids, string conversion,
`MemoryStream`, `CPluginView`), `source/main` (the factory, module init and the
three platform entry files), `source/vst` (`AudioEffect`, `EditController`,
`SingleComponentEffect`, buses, parameters, presets, representation and the
headers around them), the header-only `source/vst/utility`, and from
`source/vst/hosting` only `parameterchanges` and `eventlist`, which the tests use
to host the adapter. The rest of hosting, the samples, the wrappers, `vstgui4`, `moduleinfo`, `cmake`, `doc` and `tutorials`
are left out; `vstgui4` is under its own BSD-style licence and nothing here
needs it.

`CMakeLists.txt` in this directory builds it as the static target `vst3sdk`, in
place of the SDK's own CMake. It defines `DEVELOPMENT=1` or `RELEASE=1` per
configuration as the SDK expects, and records the platform's entry file on the
target as `MAKEASOUND_VST3_SDK_MAIN` (and the export list on macOS as
`MAKEASOUND_VST3_SDK_EXPORTS`), because `bundleEntry`, `InitDll` and
`ModuleEntry` must be compiled into the plugin binary itself, where a static
archive would let the linker drop them. The two hosting files build separately as
the test-only static target `vst3sdk-hosting`, so no plugin links them.

To update: clone `steinbergmedia/vst3sdk` at the new tag with the `base`,
`pluginterfaces` and `public.sdk` submodules, replace the folders above with the
same selection of files (`source/vst/hosting/{parameterchanges,eventlist}.{h,cpp}`
included, taken from the same tag: the `public.sdk` submodule commit it pins),
bump this note and rebuild `MakeASoundTests`, whose `VST3SDKTests.cpp` links
the target and whose `VST3/` suites link `vst3sdk-hosting`.

## AudioUnitSDK (not in this tree)

Apple's AudioUnitSDK, Apache-2.0, is the one SDK `ThirdParty/CMakeLists.txt`
builds without vendoring: on macOS it is fetched by CPM at the pinned tag
(`AudioUnitSDK-1.4.0`, `DOWNLOAD_ONLY`) and compiled as the static target
`ausdk` from its twelve `src/AudioUnitSDK/*.cpp` files, because the tag is
stable, the license is permissive, and nothing in it is trimmed or edited. Its
headers include `<expected>`, so the target carries C++23 PUBLIC and only the AU
adapter and the `-AU` modules compile under it. To move to a newer release,
change the tag.
