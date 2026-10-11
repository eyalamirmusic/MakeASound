# MakeASoundStandalone: the standalone format, a Plugin in a window on Engine. It
# needs eacp's window tier and a desktop with a menu bar: macOS and Windows today.
# Nothing shared links it; only a <Name>-Standalone executable does, which
# makeasound_add_plugin builds from the main file recorded on the target.
if (NOT TARGET MakeASoundPluginUI OR NOT TARGET eacp-graphics
        OR NOT ((APPLE AND NOT IOS) OR WIN32))
    message(STATUS "MakeASoundStandalone skipped (needs eacp graphics on a desktop)")
    return()
endif ()

add_library(MakeASoundStandalone STATIC
        StandaloneProcessor.cpp
        MidiSender.cpp
        TypingKeyboard.cpp
        Settings.cpp
        SettingsPanel.cpp
        StandaloneApp.cpp)

target_link_libraries(MakeASoundStandalone PUBLIC MakeASoundPluginUI eacp-graphics)

set_target_properties(MakeASoundStandalone PROPERTIES
        MAKEASOUND_STANDALONE_MAIN "${CMAKE_CURRENT_LIST_DIR}/StandaloneMain.cpp")

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundStandalone PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundStandalone)
