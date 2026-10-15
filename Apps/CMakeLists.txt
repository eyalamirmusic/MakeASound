set(CMAKE_FOLDER Apps)

find_package(eacp REQUIRED)

# eacp draws on Apple and Windows only - its Graphics and GPU modules sit behind
# this same condition in its own tree - so the app that needs them is gated the
# same way, and a Linux build gets the console demos.
if ((APPLE OR WIN32) AND EACP_BUILD_GRAPHICS)
    set(MAKEASOUND_BUILD_UI_APPS ON)
else ()
    set(MAKEASOUND_BUILD_UI_APPS OFF)
endif ()

# The GPU/UI app is the one that builds everywhere eacp draws, iOS included.
if (MAKEASOUND_BUILD_UI_APPS)
    add_subdirectory(AudioProbe)
endif ()

# The console demos are desktop-only.
if (NOT IOS)
    add_subdirectory(Example)
    add_subdirectory(MidiDemo)
endif ()

# The console plugin scanner and the example host, wherever hosting builds (the
# host's window where eacp draws).
if (TARGET MakeASoundHosting)
    add_subdirectory(PluginScanner)
    add_subdirectory(PluginHost)
endif ()
