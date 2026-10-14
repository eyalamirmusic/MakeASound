# PluginHost: the example plugin host, and the reference for hosting with
# MakeASound (Docs/Hosting.md). PluginHostCore is everything but the windows, so
# the tests drive it headless; the app adds a window on top.
add_library(PluginHostCore STATIC PluginHost.cpp)
target_link_libraries(PluginHostCore PUBLIC MakeASound MakeASoundHosting)
target_include_directories(PluginHostCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_makeasound_target_settings(PluginHostCore)

if (NOT (MAKEASOUND_BUILD_UI_APPS AND TARGET MakeASoundHostedEditor))
    message(STATUS "PluginHost app skipped (needs eacp graphics and the hosted editors)")
    return()
endif ()

add_executable(PluginHost Main.cpp)

# Sealed, as every host that loads third-party code should be.
makeasound_enable_hosting(PluginHost)
target_link_libraries(PluginHost PRIVATE PluginHostCore MakeASoundHostedEditor eacp-ui)

set(BUNDLE_ID "com.makeasound.pluginhost")

set_target_properties(PluginHost PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "MakeASound PluginHost"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})

eacp_set_gui_subsystem(PluginHost)

# A hosted effect gets the capture side.
eacp_add_plist_entries(PluginHost NSMicrophoneUsageDescription
        "MakeASound's PluginHost feeds the plugin it hosts from your input.")

set_makeasound_target_settings(PluginHost)
