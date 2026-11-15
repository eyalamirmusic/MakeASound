set(CMAKE_FOLDER Apps)

find_package(eacp REQUIRED)

# The app that draws needs eacp's UI and GPU modules, wherever eacp builds them.
if (TARGET eacp-ui AND TARGET eacp-gpu)
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
    add_subdirectory(ScanProtocol)
    add_subdirectory(PluginScanner)
    add_subdirectory(PluginHost)
endif ()
