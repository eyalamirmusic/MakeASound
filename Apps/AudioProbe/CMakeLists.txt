add_executable(AudioProbe
        Main.cpp
        Analyser.cpp
        AudioEngine.cpp
        Probes.cpp
        ProbePanel.cpp
        SpectrumView.cpp)

# The session probe is the one platform-specific piece: only iOS has an
# AVAudioSession for MakeASound to take over.
if (IOS)
    target_sources(AudioProbe PRIVATE AudioSession-iOS.mm)
    target_link_libraries(AudioProbe PRIVATE "-framework AVFoundation")
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

# Our own Info.plist rather than eacp's stock one: opening the capture side
# needs NSMicrophoneUsageDescription, which neither platform's default carries.
if (IOS)
    set_target_properties(AudioProbe PROPERTIES MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/Info-iOS.plist.in")
elseif (APPLE)
    set_target_properties(AudioProbe PROPERTIES MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/Info-macOS.plist.in")
endif ()

set_makeasound_warnings(AudioProbe)
