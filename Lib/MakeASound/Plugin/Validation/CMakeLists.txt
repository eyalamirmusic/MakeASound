# MakeASoundPluginval: Tracktion's pluginval fetched and run on eacp for a .vst3,
# auval for a .component and the user's own AAX Validator (aaxval in DigiShell) for
# an .aaxplugin, for the PluginValidator tool and the env-gated test. One TU per
# platform for pluginval's release asset and for the AU and AAX halves.
if (NOT TARGET eacp-network OR IOS OR ANDROID)
    message(STATUS "MakeASoundPluginval skipped (needs eacp-network on a desktop)")
    return()
endif ()

add_library(MakeASoundPluginval STATIC Pluginval.cpp AAXValidator.cpp)

if (APPLE)
    target_sources(MakeASoundPluginval PRIVATE
            Pluginval-macOS.cpp
            AUValidator-macOS.cpp
            AAXValidator-macOS.cpp)
    target_link_libraries(MakeASoundPluginval PRIVATE "-framework CoreFoundation")
elseif (WIN32)
    target_sources(MakeASoundPluginval PRIVATE
            Pluginval-Windows.cpp
            AUValidator-Default.cpp
            AAXValidator-Default.cpp)
else ()
    target_sources(MakeASoundPluginval PRIVATE
            Pluginval-Linux.cpp
            AUValidator-Default.cpp
            AAXValidator-Default.cpp)
endif ()

target_link_libraries(MakeASoundPluginval PUBLIC MakeASoundCore eacp-network)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundPluginval PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundPluginval)
