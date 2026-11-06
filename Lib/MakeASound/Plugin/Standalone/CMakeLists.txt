# MakeASoundStandaloneCore: a plugin run without a host and without a window
# (StandaloneHost over devices, MIDI, the wrapper, Engine and the settings file).
# It needs the plugin core and the device library, so it builds wherever both do,
# Linux and iOS included; an app of a plugin's own composes it with any UI.
# Where a JUCE build of the plugin kept its standalone settings, one TU per
# platform JUCE's PropertiesFile names a folder for.
if (NOT TARGET MakeASound)
    message(STATUS "MakeASoundStandaloneCore skipped (needs MAKEASOUND_BUILD_DEVICES)")
    return()
endif ()

if (APPLE)
    set(juceSettingsPath JuceSettingsPath-Apple.cpp)
elseif (WIN32)
    set(juceSettingsPath JuceSettingsPath-Windows.cpp)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(juceSettingsPath JuceSettingsPath-Linux.cpp)
else ()
    set(juceSettingsPath JuceSettingsPath-Default.cpp)
endif ()

add_library(MakeASoundStandaloneCore STATIC
        StandaloneProcessor.cpp
        MidiSender.cpp
        Settings.cpp
        ${juceSettingsPath}
        StandaloneHost.cpp
        StandaloneHostDefault.cpp)

target_link_libraries(MakeASoundStandaloneCore PUBLIC MakeASoundPlugin MakeASound eacp-core)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundStandaloneCore PROPERTIES UNITY_BUILD ON)
endif ()

# Apps/PluginHost keeps its picks through Settings.cpp alone. Out of the unity
# batch, so linking it pulls in no StandaloneHost, whose default constructor
# needs a describeModule() the host does not define.
set_source_files_properties(Settings.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

# StandaloneHost's default constructor, which runs describeModule()'s first
# plugin, in a TU of its own and out of the batch for the same reason: an app
# that builds its plugin itself and hands it to the (plugin, file) constructor
# needs no describeModule(). Tests/StandaloneLink links one.
set_source_files_properties(StandaloneHostDefault.cpp PROPERTIES
        SKIP_UNITY_BUILD_INCLUSION ON)

set_makeasound_target_settings(MakeASoundStandaloneCore)

# MakeASoundStandalone: the default app, the core's host with its editor and the
# settings panel, over eacp's window tier: on macOS and Windows a window each and
# the menu bar, on iOS one screen. The shell is one TU per platform. Nothing
# shared links it; a <Name>-Standalone executable does, which
# makeasound_add_plugin builds from the main file recorded on the target, and
# Apps/PluginHost does for SettingsPanel alone.
if (NOT TARGET MakeASoundPluginUI OR NOT TARGET eacp-graphics
        OR NOT (APPLE OR WIN32))
    message(STATUS "MakeASoundStandalone skipped (needs eacp graphics on Apple or Windows)")
    return()
endif ()

if (IOS)
    set(shell StandaloneApp-iOS.cpp)
else ()
    set(shell StandaloneApp-Desktop.cpp)
endif ()

add_library(MakeASoundStandalone STATIC
        TypingKeyboard.cpp
        SettingsPanel.cpp
        EditorWindow.cpp
        StandaloneApp.cpp
        ${shell})

target_link_libraries(MakeASoundStandalone PUBLIC
        MakeASoundStandaloneCore MakeASoundPluginUI eacp-graphics)

set_target_properties(MakeASoundStandalone PROPERTIES
        MAKEASOUND_STANDALONE_MAIN "${CMAKE_CURRENT_LIST_DIR}/StandaloneMain.cpp")

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundStandalone PROPERTIES UNITY_BUILD ON)
endif ()

# Out of the unity batch for the same reason as Settings.cpp: the host links
# the panel without StandaloneApp.
set_source_files_properties(SettingsPanel.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

set_makeasound_target_settings(MakeASoundStandalone)
