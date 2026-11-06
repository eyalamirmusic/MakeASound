# MakeASoundAUv3: the AUv3 format, an AUAudioUnit's buses, parameter tree, state
# and render block over PluginWrapper, in Objective-C++ with ARC and no SDK, so
# C++20. Built on every Apple platform: it ships on iOS, and macOS runs its suites.
# The static target defines no Objective-C class; the sources recorded as
# MAKEASOUND_AUV3_SOURCES are compiled into each module (and the tests) with
# MAKEASOUND_AUV3_CLASS_SUFFIX defined, since the runtime is one per process.
if (NOT APPLE)
    return()
endif ()

add_library(MakeASoundAUv3 STATIC
        Adapter.mm
        Conversion.mm
        Parameters.mm
        State.mm)

target_link_libraries(MakeASoundAUv3 PUBLIC
        MakeASoundPlugin
        MakeASoundAUDescribe
        "-framework AudioToolbox"
        "-framework AVFoundation"
        "-framework CoreAudioKit"
        "-framework Foundation")

# An app extension may call only extension-safe API; the compiler checks it here
# even where this archive is linked into an app.
target_compile_options(MakeASoundAUv3 PRIVATE -fobjc-arc -fapplication-extension)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundAUv3 PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundAUv3)

# The view, where eacp's UI tier is built: MakeASoundAUv3View is the half both
# platforms share (EmbeddedEditor), and each module compiles its platform's view
# controller, the extension's principal class, beside AudioUnit.mm. Without the
# tier the module compiles NoAUv3View.mm, a plain factory, and its extension is
# declared com.apple.AudioUnit rather than -UI. One source file either way, the
# choice at link time, as the v2 view's is.
if (TARGET MakeASoundPluginUI AND TARGET eacp-graphics)
    add_library(MakeASoundAUv3View STATIC EmbeddedEditor.mm)
    target_link_libraries(MakeASoundAUv3View PUBLIC
            MakeASoundAUv3 MakeASoundPluginUI eacp-graphics)
    target_compile_options(MakeASoundAUv3View PRIVATE
            -fobjc-arc -fapplication-extension)
    set_makeasound_target_settings(MakeASoundAUv3View)

    if (IOS)
        set(view_source "${CMAKE_CURRENT_LIST_DIR}/AUv3View-iOS.mm")
        set(view_libraries MakeASoundAUv3View "-framework UIKit")
    else ()
        set(view_source "${CMAKE_CURRENT_LIST_DIR}/AUv3View-macOS.mm")
        set(view_libraries MakeASoundAUv3View "-framework Cocoa")
    endif ()

    set(has_view ON)
    set(principal_class MakeASoundAUv3ViewController)
else ()
    set(view_source "${CMAKE_CURRENT_LIST_DIR}/NoAUv3View.mm")
    set(view_libraries "")
    set(has_view OFF)
    set(principal_class MakeASoundAUv3Factory)
    message(STATUS "MakeASoundAUv3: no editor view (eacp graphics not built)")
endif ()

# What every module compiles (AudioUnit.mm and the view source, with
# MAKEASOUND_AUV3_CLASS_SUFFIX defined) and links, and the principal class's name
# before the suffix, for the extension's plist (hasView picks its extension point).
# MAKEASOUND_AUV3_MAIN is the appex executable's main, a stub over NSExtensionMain.
set_target_properties(MakeASoundAUv3 PROPERTIES
        MAKEASOUND_AUV3_MAIN "${CMAKE_CURRENT_LIST_DIR}/ExtensionMain.cpp"
        MAKEASOUND_AUV3_SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/AudioUnit.mm;${view_source}"
        MAKEASOUND_AUV3_VIEW_SOURCE "${view_source}"
        MAKEASOUND_AUV3_VIEW_LIBRARIES "${view_libraries}"
        MAKEASOUND_AUV3_HAS_VIEW ${has_view}
        MAKEASOUND_AUV3_PRINCIPAL_CLASS ${principal_class})
