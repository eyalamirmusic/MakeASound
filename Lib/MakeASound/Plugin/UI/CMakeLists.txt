# MakeASoundPluginUI: the eacp-ui pieces every format shares, the generic
# parameter editor, the resize grip, and EmbeddedEditor, the editor embedded in a
# DAW's window that the VST3 and CLAP views share. Built wherever eacp has its UI
# tier.
if (NOT TARGET eacp-ui)
    message(STATUS "MakeASoundPluginUI skipped (eacp-ui not built)")
    return()
endif ()

add_library(MakeASoundPluginUI STATIC
        EmbeddedEditor.cpp
        GenericEditor.cpp
        ResizeGrip.cpp)

# The host's units per point and what becomes of a key the editor left: points
# on Apple platforms, pixels on Windows and X11, where a key also goes on to the
# host's window on Windows.
if (WIN32)
    target_sources(MakeASoundPluginUI PRIVATE EmbeddedEditor-Windows.cpp)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(MakeASoundPluginUI PRIVATE EmbeddedEditor-Linux.cpp)
else ()
    target_sources(MakeASoundPluginUI PRIVATE EmbeddedEditor-Default.cpp)
endif ()

target_link_libraries(MakeASoundPluginUI PUBLIC
        MakeASoundPlugin eacp-ui eacp-graphics)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundPluginUI PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundPluginUI)
