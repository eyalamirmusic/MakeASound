# MakeASoundVST3: the VST3 format, a single-component Adapter over PluginWrapper
# and the vendored SDK. Each <Name>-VST3 module links it and compiles the entry
# files recorded on this target, since an archive would let the linker drop them.
if (NOT TARGET vst3sdk OR IOS OR ANDROID)
    message(STATUS "MakeASoundVST3 skipped (needs the VST3 SDK on a desktop)")
    return()
endif ()

add_library(MakeASoundVST3 STATIC
        Adapter.cpp
        HostParameters.cpp
        Conversion.cpp
        Text.cpp
        Stream.cpp
        Factory.cpp)

target_link_libraries(MakeASoundVST3 PUBLIC MakeASoundPlugin vst3sdk)

# One TU per platform for what differs: the view type and scaling the host hands
# over, and the run loop it can lend (Linux's IRunLoop; nothing elsewhere).
if (APPLE)
    set(platform_view PlugView-macOS.cpp)
    set(platform_loop HostRunLoop-Default.cpp)
elseif (WIN32)
    set(platform_view PlugView-Windows.cpp)
    set(platform_loop HostRunLoop-Default.cpp)
else ()
    set(platform_view PlugView-Linux.cpp)
    set(platform_loop HostRunLoop-Linux.cpp)
endif ()

target_sources(MakeASoundVST3 PRIVATE ${platform_loop})

# One target either way: the view TUs or their null twin define createPlugView,
# so the choice is made here at link time rather than by a registering initialiser.
if (TARGET MakeASoundPluginUI AND TARGET eacp-graphics)
    target_sources(MakeASoundVST3 PRIVATE PlugView.cpp ${platform_view})
    target_link_libraries(MakeASoundVST3 PRIVATE MakeASoundPluginUI eacp-graphics)
    target_compile_definitions(MakeASoundVST3 PUBLIC MAKEASOUND_VST3_HAS_VIEW=1)
else ()
    target_sources(MakeASoundVST3 PRIVATE NoPlugView.cpp)
    message(STATUS "MakeASoundVST3: no editor view (eacp graphics not built)")
endif ()

set_target_properties(MakeASoundVST3 PROPERTIES
        MAKEASOUND_VST3_ENTRY "${CMAKE_CURRENT_LIST_DIR}/EntryPoint.cpp"
        MAKEASOUND_VST3_PLIST "${CMAKE_CURRENT_LIST_DIR}/VST3Info.plist.in"
        MAKEASOUND_VST3_PKGINFO "${CMAKE_CURRENT_LIST_DIR}/PkgInfo"
        MAKEASOUND_VST3_LINUX_EXPORTS "${CMAKE_CURRENT_LIST_DIR}/VST3Exports.map")

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundVST3 PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundVST3)
