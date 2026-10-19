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
        FormatManager.cpp
        HostedPlugin.cpp
        PluginCache.cpp
        Scan/ExecutablePath.cpp
        Scan/ScanArgs.cpp
        Scan/ScanCoordinator.cpp
        Scan/Scanner.cpp
        Scan/ScanSession.cpp
        Scan/ScanWorker.cpp
        VST3/VST3Format.cpp
        VST3/VST3HostedParameter.cpp
        VST3/VST3HostedPlugin.cpp)

# One TU per platform for what differs; CMake picks it, no #if does.
if (APPLE)
    # Audio Units, macOS only, over AudioToolbox's C API.
    target_sources(MakeASoundHosting PRIVATE
            AU/AUFormat.mm
            AU/HostedAudioUnit.cpp
            DefaultFormats-macOS.cpp
            Scan/ExecutablePath-macOS.cpp
            Scan/WorkerProcess-macOS.cpp
            VST3/VST3Folders-macOS.cpp)

    target_compile_options(MakeASoundHosting PRIVATE
            $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(MakeASoundHosting PRIVATE
            "-framework AudioToolbox"
            "-framework CoreFoundation"
            "-framework CoreMIDI")
else ()
    target_sources(MakeASoundHosting PRIVATE
            DefaultFormats-Windows.cpp
            Scan/ExecutablePath-Windows.cpp
            Scan/WorkerProcess-Windows.cpp
            VST3/VST3Folders-Windows.cpp)

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

# MakeASoundHostedEditor: a hosted plugin's own editor in an eacp view, apart so
# the hosting library itself never links graphics. A host that shows editors
# links it and passes hostedEditors() as HostInfo::editors.
if (TARGET eacp-graphics)
    add_library(MakeASoundHostedEditor STATIC
            VST3/HostedVST3Editor.cpp)

    if (APPLE)
        target_sources(MakeASoundHostedEditor PRIVATE
                AU/HostedAudioUnitEditor.mm
                HostedEditors-macOS.cpp
                VST3/VST3EditorPlatform-macOS.cpp)

        target_compile_options(MakeASoundHostedEditor PRIVATE
                $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
        target_link_libraries(MakeASoundHostedEditor PRIVATE
                "-framework AudioToolbox"
                "-framework Cocoa"
                "-framework CoreAudioKit")
    else ()
        target_sources(MakeASoundHostedEditor PRIVATE
                HostedEditors-Windows.cpp
                VST3/VST3EditorPlatform-Windows.cpp)

        target_compile_definitions(MakeASoundHostedEditor PRIVATE
                WIN32_LEAN_AND_MEAN NOMINMAX)
    endif ()

    target_link_libraries(MakeASoundHostedEditor
            PUBLIC MakeASoundHosting
            PRIVATE eacp-graphics vst3sdk-hosting)

    if (MAKEASOUND_UNITY_BUILD)
        set_target_properties(MakeASoundHostedEditor PROPERTIES UNITY_BUILD ON)
    endif ()

    set_makeasound_target_settings(MakeASoundHostedEditor)
else ()
    message(STATUS "MakeASoundHostedEditor skipped (needs eacp graphics)")
endif ()

# makeasound_enable_hosting(), for this tree's apps and for a consumer's own.
include(MakeASoundHosting)
