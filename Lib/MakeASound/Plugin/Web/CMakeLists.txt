# MakeASoundPluginWeb: the page API and the Editor over eacp's WebView, for a
# plugin whose editor is a web page. Built where eacp has a native web runtime
# (macOS and Windows); makeasound_add_web_editor() falls back without it.
if (NOT TARGET eacp-webview OR NOT TARGET MakeASoundPluginUI)
    message(STATUS "MakeASoundPluginWeb skipped (eacp-webview not built)")
    return()
endif ()

add_library(MakeASoundPluginWeb STATIC
        ParameterApi.cpp
        WebEditor.cpp)

target_link_libraries(MakeASoundPluginWeb PUBLIC MakeASoundPluginUI eacp-webview)

# Read by makeasound_add_web_editor(), whose caller's directory cannot see ours.
set_target_properties(MakeASoundPluginWeb PROPERTIES
        MAKEASOUND_WEB_API_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/ParameterApi.h")

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundPluginWeb PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundPluginWeb)
