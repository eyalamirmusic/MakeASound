# Not "Synth": Apps/Synth, the Engine demo that plays the same TestSynth, owns that name.
makeasound_add_plugin(SynthPlugin
        FORMATS Standalone VST3
        OUTPUT_NAME "MakeASound Synth"
        BUNDLE_ID com.makeasound.synth
        SOURCES SynthPlugin.cpp)

target_link_libraries(SynthPlugin PUBLIC MakeASoundDSP)
