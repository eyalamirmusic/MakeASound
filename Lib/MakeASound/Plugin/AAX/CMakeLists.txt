# MakeASoundAAX: the AAX format, one AAX_CEffectParameters Adapter over
# PluginWrapper and Avid's AAX SDK, which is fetched only with MAKEASOUND_BUILD_AAX.
# Each <Name>-AAX module links it and compiles the entry recorded on this target,
# since the entry holds the SDK's ACF glue and the module's own bundle id.
if (NOT TARGET aaxsdk)
    if (MAKEASOUND_BUILD_AAX)
        message(STATUS "MakeASoundAAX skipped (needs the AAX SDK, macOS only)")
    endif ()

    return()
endif ()

add_library(MakeASoundAAX STATIC
        Adapter.cpp
        Conversion.cpp
        Describe.cpp
        Parameters.cpp
        State.cpp)

target_link_libraries(MakeASoundAAX PUBLIC MakeASoundPlugin aaxsdk)

# The view TUs or their null twin define createAAXView and hasAAXView, so the
# choice is made here at link time; the native container type is per platform.
if (TARGET MakeASoundPluginUI AND TARGET eacp-graphics)
    target_sources(MakeASoundAAX PRIVATE View.cpp View-macOS.mm)
    set_source_files_properties(View-macOS.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(MakeASoundAAX PRIVATE MakeASoundPluginUI eacp-graphics)
    target_compile_definitions(MakeASoundAAX PUBLIC MAKEASOUND_AAX_HAS_VIEW=1)
else ()
    target_sources(MakeASoundAAX PRIVATE NoView.cpp)
    message(STATUS "MakeASoundAAX: no editor view (eacp graphics not built)")
endif ()

set_target_properties(MakeASoundAAX PROPERTIES
        MAKEASOUND_AAX_ENTRY "${CMAKE_CURRENT_LIST_DIR}/EntryPoint.cpp"
        MAKEASOUND_AAX_EXPORTS "${CMAKE_CURRENT_LIST_DIR}/AAXExports.txt"
        MAKEASOUND_AAX_PLIST "${CMAKE_CURRENT_LIST_DIR}/AAXInfo.plist.in"
        MAKEASOUND_AAX_PKGINFO "${CMAKE_CURRENT_LIST_DIR}/PkgInfo")

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundAAX PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundAAX)
