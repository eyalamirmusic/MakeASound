set(CMAKE_FOLDER Plugins)

add_subdirectory(Gain)
add_subdirectory(Synth)

# A custom standalone app needs the default one's window tier to compose, and
# this one is a desktop app: a second window and a menu bar.
if (TARGET MakeASoundStandalone AND NOT IOS)
    add_subdirectory(GainApp)
else ()
    message(STATUS "GainApp skipped (a desktop app: needs MakeASoundStandalone, not iOS)")
endif ()
