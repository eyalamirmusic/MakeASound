# AUv3Probe: what a host does with an installed AUv3, as a command-line tool for
# `xcrun simctl spawn`. iOS only, where the extensions ship.
add_executable(AUv3Probe AUv3Probe.mm)
target_link_libraries(AUv3Probe PRIVATE
        "-framework AudioToolbox"
        "-framework AVFoundation"
        "-framework CoreAudioKit"
        "-framework Foundation"
        "-framework UIKit")
target_compile_options(AUv3Probe PRIVATE -fobjc-arc)
set_target_properties(AUv3Probe PROPERTIES MACOSX_BUNDLE FALSE)
set_makeasound_target_settings(AUv3Probe)
