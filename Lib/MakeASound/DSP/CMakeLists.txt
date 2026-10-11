set(CMAKE_FOLDER Lib/DSP)

# MakeASoundDSP: processors built on the device library alone, for an app or a
# plugin to link when it wants one. Nothing in the library links it.
add_library(MakeASoundDSP STATIC
        TestSynth.cpp)

target_link_libraries(MakeASoundDSP PUBLIC MakeASound)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundDSP PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundDSP)
