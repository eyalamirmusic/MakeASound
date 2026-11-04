set(CMAKE_FOLDER Lib/Files)

# MakeASoundFiles: WAV reading and writing and AIFF reading over byte sources and
# sinks, built on the core vocabulary alone. Nothing in the library links it.
add_library(MakeASoundFiles STATIC
        MemoryStreams.cpp
        FileStreams.cpp
        SampleConversion.cpp
        WavReader.cpp
        WavWriter.cpp
        AiffReader.cpp)

add_library(MakeASound::Files ALIAS MakeASoundFiles)

target_link_libraries(MakeASoundFiles PUBLIC MakeASoundCore)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundFiles PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundFiles)
