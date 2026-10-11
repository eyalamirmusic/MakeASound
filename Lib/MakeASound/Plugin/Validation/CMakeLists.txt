# MakeASoundPluginval: Tracktion's pluginval fetched and run on eacp for a .vst3,
# auval for a .component, for the PluginValidator tool and the env-gated test. One
# TU per platform for pluginval's release asset and for the AU half.
if (NOT TARGET eacp-network OR IOS OR ANDROID)
    message(STATUS "MakeASoundPluginval skipped (needs eacp-network on a desktop)")
    return()
endif ()

add_library(MakeASoundPluginval STATIC Pluginval.cpp)

if (APPLE)
    target_sources(MakeASoundPluginval PRIVATE
            Pluginval-macOS.cpp
            AUValidator-macOS.cpp)
    target_link_libraries(MakeASoundPluginval PRIVATE "-framework CoreFoundation")
elseif (WIN32)
    target_sources(MakeASoundPluginval PRIVATE
            Pluginval-Windows.cpp
            AUValidator-Default.cpp)
else ()
    target_sources(MakeASoundPluginval PRIVATE
            Pluginval-Linux.cpp
            AUValidator-Default.cpp)
endif ()

target_link_libraries(MakeASoundPluginval PUBLIC MakeASound eacp-network)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundPluginval PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundPluginval)
