# Fetch RtMidi's source via CPM but skip its CMake — we build our own
# minimal `rtmidi` STATIC target with just RtMidi.cpp and the per-platform
# defines/libs we actually need (CoreMIDI on macOS, WinMM on Windows,
# ALSA on Linux). This avoids RtMidi's PkgConfig probes, jack auto-detection,
# the rtmidi_c.cpp C wrapper, and shared-lib generation.
#
# To point at a local checkout instead of fetching, configure with
# -DCPM_RTMidi_SOURCE="Path_To_RTMidi".

include(CPM)

message(STATUS "Fetching RtMidi (first configure only)...")
CPMAddPackage(
        NAME RTMidi
        URL https://github.com/thestk/rtmidi/archive/refs/tags/6.0.0.zip
        DOWNLOAD_ONLY YES
        DOWNLOAD_NO_PROGRESS YES
        QUIET
)

add_library(rtmidi STATIC ${RTMidi_SOURCE_DIR}/RtMidi.cpp)
target_include_directories(rtmidi SYSTEM PUBLIC ${RTMidi_SOURCE_DIR})

if (APPLE)
    target_compile_definitions(rtmidi PRIVATE __MACOSX_CORE__)
    target_link_libraries(rtmidi PRIVATE
            "-framework CoreMIDI"
            "-framework CoreAudio"
            "-framework CoreFoundation")
elseif (WIN32)
    target_compile_definitions(rtmidi PRIVATE __WINDOWS_MM__)
    target_link_libraries(rtmidi PRIVATE winmm)
elseif (UNIX)
    find_package(ALSA REQUIRED)
    find_package(Threads REQUIRED)
    target_compile_definitions(rtmidi PRIVATE __LINUX_ALSA__)
    target_link_libraries(rtmidi PRIVATE ALSA::ALSA Threads::Threads)
endif ()

# RtMidi's source has a few non-standard constructs that older clang/gcc
# warn on. They're upstream and harmless — silence them so our own build
# output stays clean.
if (MSVC)
    target_compile_options(rtmidi PRIVATE /w)
else ()
    target_compile_options(rtmidi PRIVATE -w)
endif ()
