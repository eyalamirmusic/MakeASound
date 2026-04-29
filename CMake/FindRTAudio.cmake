# Fetch RtAudio's source via CPM but skip its CMake — we build our own
# minimal `rtaudio` STATIC target with just RtAudio.cpp and the per-platform
# defines/libs we actually need (CoreAudio on macOS, WASAPI on Windows,
# ALSA on Linux). This avoids RtAudio's PkgConfig probes, jack/pulse
# auto-detection, the rtaudio_c.cpp C wrapper, and shared-lib generation.
#
# To point at a local checkout instead of fetching, configure with
# -DCPM_RTAudio_SOURCE="Path_To_RTAudio".

include(CPM)

CPMAddPackage(
        NAME RTAudio
        URL https://github.com/thestk/rtaudio/archive/refs/tags/6.0.1.zip
        DOWNLOAD_ONLY YES
        DOWNLOAD_NO_PROGRESS YES
        LOG_DOWNLOAD YES
        LOG_EXTRACT YES
)

add_library(rtaudio STATIC ${RTAudio_SOURCE_DIR}/RtAudio.cpp)
target_include_directories(rtaudio SYSTEM PUBLIC ${RTAudio_SOURCE_DIR})


target_compile_definitions(rtaudio PRIVATE HAVE_GETTIMEOFDAY)


if (APPLE)
    target_compile_definitions(rtaudio PRIVATE __MACOSX_CORE__)
    target_link_libraries(rtaudio PRIVATE
            "-framework CoreAudio"
            "-framework CoreFoundation")
elseif (WIN32)
    target_compile_definitions(rtaudio PRIVATE __WINDOWS_WASAPI__)
    # WASAPI helper headers (functiondiscoverykeys_devpkey.h) live here.
    target_include_directories(rtaudio PRIVATE ${RTAudio_SOURCE_DIR}/include)
    target_link_libraries(rtaudio PRIVATE
            ksuser mfplat mfuuid wmcodecdspuuid winmm ole32)
elseif (UNIX)
    find_package(ALSA REQUIRED)
    find_package(Threads REQUIRED)
    target_compile_definitions(rtaudio PRIVATE __LINUX_ALSA__)
    target_link_libraries(rtaudio PRIVATE ALSA::ALSA Threads::Threads)
endif ()

# RtAudio's source has a few non-standard constructs (VLAs) that clang/gcc
# warn on. They're upstream and harmless — silence them so our own build
# output stays clean.
if (MSVC)
    target_compile_options(rtaudio PRIVATE /w)
else ()
    target_compile_options(rtaudio PRIVATE -w)
endif ()
