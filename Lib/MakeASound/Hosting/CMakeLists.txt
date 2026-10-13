set(CMAKE_FOLDER Lib/Hosting)

# MakeASoundHosting: scanning third-party plugins, in process or out of it, and the
# cache of what a scan found. Deliberately slim, so a scanner's worker process can
# link it: eacp-core (processes, timers, files) and the format SDKs, and no
# graphics. Hosted editors go in a library of their own.
#
# macOS and Windows for now. Linux and iOS hosting come later, and the platform
# split below is where they slot in.
if (NOT ((APPLE AND NOT IOS) OR WIN32))
    message(STATUS "MakeASoundHosting skipped (macOS and Windows only for now)")
    return()
endif ()

if (NOT TARGET vst3sdk-hosting)
    message(STATUS "MakeASoundHosting skipped (needs the VST3 SDK's hosting sources)")
    return()
endif ()

find_package(eacp REQUIRED)

add_library(MakeASoundHosting STATIC
        PluginCache.cpp
        Scan/ExecutablePath.cpp
        Scan/Scanner.cpp)

# One TU per platform for what differs; CMake picks it, no #if does.
if (APPLE)
    target_sources(MakeASoundHosting PRIVATE
            Scan/ExecutablePath-macOS.cpp)
else ()
    target_sources(MakeASoundHosting PRIVATE
            Scan/ExecutablePath-Windows.cpp)

    # On the target, so windows.h leaves no min/max macros in a unity chunk.
    target_compile_definitions(MakeASoundHosting PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif ()

target_link_libraries(MakeASoundHosting
        PUBLIC MakeASoundPlugin
        PRIVATE eacp-core vst3sdk-hosting)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundHosting PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundHosting)

# makeasound_enable_hosting(), for this tree's apps and for a consumer's own.
include(MakeASoundHosting)
