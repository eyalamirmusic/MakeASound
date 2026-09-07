find_package(eacp REQUIRED)

# The GPU/UI app is the one that builds everywhere eacp draws, iOS included.
add_subdirectory(AudioProbe)

# The console demos and the webview apps are desktop-only.
if (NOT IOS)
    add_subdirectory(Example)
    add_subdirectory(MidiDemo)
    add_subdirectory(Demo)
    add_subdirectory(Synth)
endif ()
