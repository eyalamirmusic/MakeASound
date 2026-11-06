# The Gain plugin in an app of its own: STANDALONE_SOURCES in place of the
# default standalone, the packaging unchanged.
makeasound_add_plugin(GainApp
        FORMATS Standalone
        OUTPUT_NAME "MakeASound Gain App"
        SOURCES ../Gain/GainPlugin.cpp
        STANDALONE_SOURCES GainApp.cpp)
