# MakeASoundAU: the AU format, one MusicDeviceBase Adapter over PluginWrapper and
# Apple's AudioUnitSDK. Each <Name>-AU module links it and compiles the entry and
# view files recorded on this target, since an archive would let the linker drop
# them, and since the view's Objective-C classes are named per module.
if (NOT TARGET ausdk)
    message(STATUS "MakeASoundAU skipped (needs the AudioUnitSDK, macOS only)")
    return()
endif ()

# What the plist generator needs, SDK-free and C++20.
add_library(MakeASoundAUDescribe STATIC ComponentType.cpp)
target_link_libraries(MakeASoundAUDescribe PUBLIC MakeASoundPlugin
        "-framework AudioToolbox")
set_makeasound_target_settings(MakeASoundAUDescribe)

add_library(MakeASoundAU STATIC
        Adapter.cpp
        HostParameters.cpp
        Conversion.cpp
        State.cpp)

target_link_libraries(MakeASoundAU PUBLIC MakeASoundPlugin MakeASoundAUDescribe ausdk)

# One source file either way: the view TU or its null twin defines cocoaViewInfo.
if (TARGET MakeASoundPluginUI AND TARGET eacp-graphics)
    set(view_source "${CMAKE_CURRENT_LIST_DIR}/CocoaUI.mm")
    set(view_libraries MakeASoundPluginUI eacp-graphics "-framework Cocoa")
    target_compile_definitions(MakeASoundAU PUBLIC MAKEASOUND_AU_HAS_VIEW=1)
else ()
    set(view_source "${CMAKE_CURRENT_LIST_DIR}/NoCocoaUI.cpp")
    set(view_libraries "")
    message(STATUS "MakeASoundAU: no editor view (eacp graphics not built)")
endif ()

set_target_properties(MakeASoundAU PROPERTIES
        MAKEASOUND_AU_ENTRY "${CMAKE_CURRENT_LIST_DIR}/EntryPoint.cpp"
        MAKEASOUND_AU_PLIST_GEN "${CMAKE_CURRENT_LIST_DIR}/PlistGen.cpp"
        MAKEASOUND_AU_EXPORTS "${CMAKE_CURRENT_LIST_DIR}/AUExports.txt"
        MAKEASOUND_AU_PKGINFO "${CMAKE_CURRENT_LIST_DIR}/../VST3/PkgInfo"
        MAKEASOUND_AU_VIEW_SOURCE "${view_source}"
        MAKEASOUND_AU_VIEW_LIBRARIES "${view_libraries}")

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundAU MakeASoundAUDescribe PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundAU)
