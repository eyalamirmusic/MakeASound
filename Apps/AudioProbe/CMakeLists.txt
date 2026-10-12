add_executable(AudioProbe
        Main.cpp
        Analyser.cpp
        AudioEngine.cpp
        Probes.cpp
        ProbePanel.cpp
        SpectrumView.cpp)

# The bundle check is the one platform-specific piece left: the session itself is
# MakeASound's to report now.
if (IOS)
    target_sources(AudioProbe PRIVATE AudioSession-iOS.mm)
    target_link_libraries(AudioProbe PRIVATE "-framework Foundation")
else ()
    target_sources(AudioProbe PRIVATE AudioSession.cpp)
endif ()

target_link_libraries(AudioProbe PRIVATE MakeASound eacp-ui eacp-gpu)

set(BUNDLE_ID "com.eyalamir.makeasound.audioprobe")

set_target_properties(AudioProbe PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "MakeASound Probe"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})

eacp_set_gui_subsystem(AudioProbe)
set_default_target_setting(AudioProbe)

# Opening the capture side needs a usage description on macOS and iOS alike.
eacp_add_plist_entries(AudioProbe NSMicrophoneUsageDescription
        "MakeASound's probe opens a capture stream to show what asking for one costs.")

set_makeasound_target_settings(AudioProbe)
