set(CMAKE_FOLDER Lib/Hosting)

# MakeASoundHosting: scanning third-party plugins (VST3, Audio Units on macOS,
# CLAP with MAKEASOUND_BUILD_CLAP), in process or out of it, and the
# cache of what a scan found. Deliberately slim, so a scanner's worker process can
# link it: eacp-core (processes, timers, files) and the format SDKs, and no
# graphics. Hosted editors go in a library of their own.
#
# macOS, Windows and Linux. iOS hosting comes later, and the platform split below
# is where it slots in.
set(hosting_linux OFF)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(hosting_linux ON)
endif ()

if (NOT ((APPLE AND NOT IOS) OR WIN32 OR hosting_linux))
    message(STATUS "MakeASoundHosting skipped (macOS, Windows and Linux only for now)")
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
        JuceHostCompat.cpp
        PathText.cpp
        PluginCache.cpp
        Scan/ExecutablePath.cpp
        Scan/ScanCoordinator.cpp
        Scan/Scanner.cpp
        Scan/ScanSession.cpp
        VST3/VST3Format.cpp
        VST3/VST3HostedParameter.cpp
        VST3/VST3HostedPlugin.cpp
        VST3/VST3Strings.cpp)

# One TU per platform for what differs; CMake picks it, no #if does.
if (APPLE)
    # Audio Units, macOS only, over AudioToolbox's C API.
    target_sources(MakeASoundHosting PRIVATE
            AU/AUFormat.mm
            AU/HostedAudioUnit.cpp
            DefaultFormats-macOS.cpp
            PathText-Default.cpp
            Scan/ExecutablePath-macOS.cpp
            Scan/StampClock-Default.cpp
            Scan/WorkerProcess-macOS.cpp
            VST3/VST3Folders-macOS.cpp)

    target_compile_options(MakeASoundHosting PRIVATE
            $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(MakeASoundHosting PRIVATE
            "-framework AudioToolbox"
            "-framework CoreFoundation"
            "-framework CoreMIDI")
elseif (WIN32)
    target_sources(MakeASoundHosting PRIVATE
            DefaultFormats-Default.cpp
            PathText-Windows.cpp
            Scan/ExecutablePath-Windows.cpp
            Scan/StampClock-Default.cpp
            Scan/WorkerProcess-Windows.cpp
            VST3/VST3Folders-Windows.cpp)

    # On the target, so windows.h leaves no min/max macros in a unity chunk.
    target_compile_definitions(MakeASoundHosting PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
else ()
    target_sources(MakeASoundHosting PRIVATE
            DefaultFormats-Default.cpp
            PathText-Default.cpp
            Scan/ExecutablePath-Linux.cpp
            Scan/StampClock-Linux.cpp
            Scan/WorkerProcess-Linux.cpp
            VST3/VST3Folders-Linux.cpp)
endif ()

# The run loop a Linux plugin asks its host for, over eacp's loop; VST3 names
# none elsewhere.
if (hosting_linux)
    target_sources(MakeASoundHosting PRIVATE VST3/VST3RunLoop-Linux.cpp)
else ()
    target_sources(MakeASoundHosting PRIVATE VST3/VST3RunLoop-Default.cpp)
endif ()

# CLAP, over the C API alone, where the headers are (MAKEASOUND_BUILD_CLAP, on
# the desktop). No public hosting header includes them: CLAPFormat.h declares the
# format, and addDefaultFormats() reaches it through DefaultFormatsCLAP-On.cpp,
# or -Off.cpp where CLAP is not built.
if (MAKEASOUND_BUILD_CLAP AND TARGET clap)
    target_sources(MakeASoundHosting PRIVATE
            CLAP/CLAPEvents.cpp
            CLAP/CLAPFolders.cpp
            CLAP/CLAPFormat.cpp
            CLAP/CLAPHost.cpp
            CLAP/CLAPHostedParameter.cpp
            CLAP/CLAPHostedPlugin.cpp
            CLAP/CLAPModule.cpp
            CLAP/CLAPStreams.cpp
            DefaultFormatsCLAP-On.cpp)

    # macOS loads a .clap bundle through CFBundle; elsewhere it is the shared
    # library itself, loaded through eacp. Only Linux offers a loaded plugin
    # clap.posix-fd-support, over eacp's loop.
    if (APPLE)
        target_sources(MakeASoundHosting PRIVATE
                CLAP/CLAPFdWatches-Default.cpp
                CLAP/CLAPFolders-macOS.cpp
                CLAP/CLAPModule-macOS.cpp)
    elseif (WIN32)
        target_sources(MakeASoundHosting PRIVATE
                CLAP/CLAPFdWatches-Default.cpp
                CLAP/CLAPFolders-Windows.cpp
                CLAP/CLAPModule-Default.cpp)
    else ()
        target_sources(MakeASoundHosting PRIVATE
                CLAP/CLAPFdWatches-Linux.cpp
                CLAP/CLAPFolders-Linux.cpp
                CLAP/CLAPModule-Default.cpp)
    endif ()

    target_link_libraries(MakeASoundHosting PRIVATE clap)
else ()
    target_sources(MakeASoundHosting PRIVATE DefaultFormatsCLAP-Off.cpp)
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
    elseif (WIN32)
        target_sources(MakeASoundHostedEditor PRIVATE
                HostedEditors-Windows.cpp
                VST3/VST3EditorPlatform-Windows.cpp)

        target_compile_definitions(MakeASoundHostedEditor PRIVATE
                WIN32_LEAN_AND_MEAN NOMINMAX)
    else ()
        # X11 only: an editor shows in a window on eacp's X11 backend.
        target_sources(MakeASoundHostedEditor PRIVATE
                HostedEditors-Linux.cpp
                VST3/VST3EditorPlatform-Linux.cpp)
    endif ()

    # CLAP editors where the hosting library hosts CLAP: hostedEditors() reaches
    # them through HostedEditorsCLAP-On.cpp, or -Off.cpp (none) elsewhere.
    if (MAKEASOUND_BUILD_CLAP AND TARGET clap)
        target_sources(MakeASoundHostedEditor PRIVATE
                CLAP/HostedCLAPEditor.cpp
                HostedEditorsCLAP-On.cpp)

        if (APPLE)
            target_sources(MakeASoundHostedEditor PRIVATE
                    CLAP/CLAPEditorPlatform-macOS.cpp)
        elseif (WIN32)
            target_sources(MakeASoundHostedEditor PRIVATE
                    CLAP/CLAPEditorPlatform-Windows.cpp)
        else ()
            target_sources(MakeASoundHostedEditor PRIVATE
                    CLAP/CLAPEditorPlatform-Linux.cpp)
        endif ()

        target_link_libraries(MakeASoundHostedEditor PRIVATE clap)
    else ()
        target_sources(MakeASoundHostedEditor PRIVATE HostedEditorsCLAP-Off.cpp)
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
