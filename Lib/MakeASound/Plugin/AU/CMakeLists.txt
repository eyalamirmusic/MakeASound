# MakeASoundAUDescribe: the component's description, both plists, the host's view
# of a parameter and the v2 view's placement geometry. SDK-free and C++20, so it
# builds on every Apple platform, iOS included, where the AUv3 adapter uses it.
if (NOT APPLE)
    return()
endif ()

add_library(MakeASoundAUDescribe STATIC
        ComponentType.cpp
        HostParameters.cpp
        Plist.cpp
        ViewPlacement.cpp)
target_link_libraries(MakeASoundAUDescribe PUBLIC MakeASoundPlugin
        "-framework AudioToolbox" "-framework CoreFoundation")
set_makeasound_target_settings(MakeASoundAUDescribe)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundAUDescribe PROPERTIES UNITY_BUILD ON)
endif ()

# The main of <Name>-Describe, which an iOS tree's host sub-build links against
# the plugin's core to write the AUv3 app extension's Info.plist.
set_target_properties(MakeASoundAUDescribe PROPERTIES
        MAKEASOUND_AU_DESCRIBE_MAIN "${CMAKE_CURRENT_LIST_DIR}/DescribeMain.cpp")

# MakeASoundAU: the AU format, one MusicDeviceBase Adapter over PluginWrapper and
# Apple's AudioUnitSDK. Each <Name>-AU module links it and compiles the entry and
# view files recorded on this target, since an archive would let the linker drop
# them, and since the view's Objective-C classes are named per module.
if (NOT TARGET ausdk)
    message(STATUS "MakeASoundAU skipped (needs the AudioUnitSDK, macOS only)")
    return()
endif ()

add_library(MakeASoundAU STATIC
        Adapter.cpp
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
        MAKEASOUND_AU_EXPORTS "${CMAKE_CURRENT_LIST_DIR}/AUExports.txt"
        MAKEASOUND_AU_PKGINFO "${CMAKE_CURRENT_LIST_DIR}/../VST3/PkgInfo"
        MAKEASOUND_AU_VIEW_SOURCE "${view_source}"
        MAKEASOUND_AU_VIEW_LIBRARIES "${view_libraries}")

# One tool for every module: it loads the module it is given and calls the
# MakeASoundAUWritePlist the module exports.
add_executable(MakeASoundAUPlistGen PlistGen.cpp)
set_makeasound_target_settings(MakeASoundAUPlistGen)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundAU PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundAU)
