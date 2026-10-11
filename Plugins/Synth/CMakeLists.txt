# Not "Synth": Apps/Synth, the Engine demo that plays the same TestSynth, owns that name.
makeasound_add_plugin(SynthPlugin
        FORMATS Standalone VST3 AU
        OUTPUT_NAME "MakeASound Synth"
        BUNDLE_ID com.makeasound.synth
        SOURCES SynthPlugin.cpp)

target_link_libraries(SynthPlugin PUBLIC MakeASoundDSP)

# The React page in web/, embedded into the core; the generic editor without a
# web runtime.
makeasound_add_web_editor(SynthPlugin
        WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/web
        SOURCES SynthEditor-Web.cpp
        FALLBACK_SOURCES SynthEditor-None.cpp)
