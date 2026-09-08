find_package(eacp REQUIRED)

# eacp draws on Apple and Windows only - its Graphics, GPU and WebView modules sit
# behind this same condition in its own tree - so the apps that need them are
# gated the same way, and a Linux build gets the console demos.
if ((APPLE OR WIN32) AND EACP_BUILD_GRAPHICS)
    set(MAKEASOUND_BUILD_UI_APPS ON)
else ()
    set(MAKEASOUND_BUILD_UI_APPS OFF)
endif ()

# The GPU/UI app is the one that builds everywhere eacp draws, iOS included.
if (MAKEASOUND_BUILD_UI_APPS)
    add_subdirectory(AudioProbe)
endif ()

# The console demos and the webview apps are desktop-only.
if (NOT IOS)
    add_subdirectory(Example)
    add_subdirectory(MidiDemo)

    if (MAKEASOUND_BUILD_UI_APPS)
        add_subdirectory(Demo)
        add_subdirectory(Synth)
    endif ()
endif ()
