set(CMAKE_FOLDER Lib/Files)

# MakeASoundFiles: WAV reading and writing and AIFF reading over byte sources and
# sinks, built on the core vocabulary alone. Nothing in the library links it.
add_library(MakeASoundFiles STATIC
        MemoryStreams.cpp
        FileStreams.cpp
        SampleConversion.cpp
        FrameReading.cpp
        WavReader.cpp
        WavWriter.cpp
        AiffReader.cpp)

# Opening a path and 64-bit file offsets: _wfsopen and _fseeki64/_ftelli64 on
# Windows, fopen and fseeko/ftello (with a 64-bit off_t) elsewhere.
if (WIN32)
    target_sources(MakeASoundFiles PRIVATE FileAccess-Windows.cpp)
else ()
    target_sources(MakeASoundFiles PRIVATE FileAccess-Default.cpp)
    target_compile_definitions(MakeASoundFiles PRIVATE _FILE_OFFSET_BITS=64)
endif ()

add_library(MakeASound::Files ALIAS MakeASoundFiles)

target_link_libraries(MakeASoundFiles PUBLIC MakeASoundCore)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundFiles PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundFiles)
