# MakeASoundPluginval: Tracktion's pluginval fetched and run on eacp, for the
# PluginValidator tool and the env-gated test. One TU per platform for the release
# asset and the binary inside it.
if (NOT TARGET eacp-network OR IOS OR ANDROID)
    message(STATUS "MakeASoundPluginval skipped (needs eacp-network on a desktop)")
    return()
endif ()

add_library(MakeASoundPluginval STATIC Pluginval.cpp)

if (APPLE)
    target_sources(MakeASoundPluginval PRIVATE Pluginval-macOS.cpp)
elseif (WIN32)
    target_sources(MakeASoundPluginval PRIVATE Pluginval-Windows.cpp)
else ()
    target_sources(MakeASoundPluginval PRIVATE Pluginval-Linux.cpp)
endif ()

target_link_libraries(MakeASoundPluginval PUBLIC MakeASound eacp-network)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundPluginval PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundPluginval)
