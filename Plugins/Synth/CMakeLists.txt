# Not "Synth": Apps/Synth, the Engine demo this was ported from, owns that name.
makeasound_add_plugin(SynthPlugin
        FORMATS Standalone
        OUTPUT_NAME "MakeASound Synth"
        BUNDLE_ID com.makeasound.synth
        SOURCES SynthPlugin.cpp)
