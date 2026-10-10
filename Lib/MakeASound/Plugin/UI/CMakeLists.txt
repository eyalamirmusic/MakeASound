# MakeASoundPluginUI: the eacp-ui pieces every format shares, today the generic
# parameter editor. Built wherever eacp has its UI tier.
if (NOT TARGET eacp-ui)
    message(STATUS "MakeASoundPluginUI skipped (eacp-ui not built)")
    return()
endif ()

add_library(MakeASoundPluginUI STATIC
        GenericEditor.cpp)

target_link_libraries(MakeASoundPluginUI PUBLIC MakeASoundPlugin eacp-ui)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundPluginUI PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_warnings(MakeASoundPluginUI)
