# MakeASoundCLAP: the CLAP format, an Adapter over PluginWrapper and the CLAP C
# headers (no clap-helpers). Each <Name>-CLAP module links it and compiles the
# entry file recorded on this target, since an archive would let the linker drop
# it, and the plugin's own describeModule() is the module's. Built only with
# MAKEASOUND_BUILD_CLAP, which fetches the headers; off, a consumer's own `clap`
# target still builds no format.
if (NOT MAKEASOUND_BUILD_CLAP OR NOT TARGET clap OR IOS OR ANDROID)
    return()
endif ()

add_library(MakeASoundCLAP STATIC
        Adapter.cpp
        Events.cpp
        Factory.cpp
        Params.cpp
        Ports.cpp
        State.cpp)

target_link_libraries(MakeASoundCLAP PUBLIC MakeASoundPlugin clap)

# The message loop pumped from the host's (Linux's posix-fd-support or a timer;
# nothing elsewhere, where the platform's own loop serves us).
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(MakeASoundCLAP PRIVATE Posix-Linux.cpp)
else ()
    target_sources(MakeASoundCLAP PRIVATE Posix-Default.cpp)
endif ()

# The editor view, clap.gui: the window API and the native handle differ per
# platform, as do VST3's view types.
if (APPLE)
    set(platform_gui Gui-macOS.cpp)
elseif (WIN32)
    set(platform_gui Gui-Windows.cpp)
else ()
    set(platform_gui Gui-Linux.cpp)
endif ()

# One target either way: the view TUs or their null twin define guiExtension, so
# the choice is made here at link time, as VST3's PlugView or NoPlugView.
if (TARGET MakeASoundPluginUI AND TARGET eacp-graphics)
    target_sources(MakeASoundCLAP PRIVATE Gui.cpp ${platform_gui})
    target_link_libraries(MakeASoundCLAP PRIVATE MakeASoundPluginUI eacp-graphics)
    target_compile_definitions(MakeASoundCLAP PUBLIC MAKEASOUND_CLAP_HAS_GUI=1)
else ()
    target_sources(MakeASoundCLAP PRIVATE NoGui.cpp)
    message(STATUS "MakeASoundCLAP: no editor view (eacp graphics not built)")
endif ()

set_target_properties(MakeASoundCLAP PROPERTIES
        MAKEASOUND_CLAP_ENTRY "${CMAKE_CURRENT_LIST_DIR}/EntryPoint.cpp"
        MAKEASOUND_CLAP_PLIST "${CMAKE_CURRENT_LIST_DIR}/CLAPInfo.plist.in"
        MAKEASOUND_CLAP_PKGINFO "${CMAKE_CURRENT_LIST_DIR}/PkgInfo"
        MAKEASOUND_CLAP_MAC_EXPORTS "${CMAKE_CURRENT_LIST_DIR}/CLAPExports.txt"
        MAKEASOUND_CLAP_LINUX_EXPORTS "${CMAKE_CURRENT_LIST_DIR}/CLAPExports.map")

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundCLAP PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundCLAP)
