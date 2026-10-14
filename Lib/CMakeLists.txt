set(CMAKE_FOLDER Lib)

# MakeASoundCore: the audio and MIDI vocabulary and the processing types (Buffer,
# Channel, BusLayout, Playhead, ProcessSpec/ProcessContext, Processor, MIDI events,
# MidiParser, the realtime helpers). No device backend: it includes no Devices/
# header and links no miniaudio and no platform audio or MIDI framework, only Miro.
# Umbrella: <MakeASound/MakeASoundCore.h>.
add_library(MakeASoundCore STATIC
        MakeASound/Audio/Buffer.cpp
        MakeASound/Audio/ProcessContext.cpp
        MakeASound/MIDI/MidiInfo.cpp
        MakeASound/MIDI/MidiParser.cpp
        MakeASound/MIDI/MIDI.cpp)

add_library(MakeASound::Core ALIAS MakeASoundCore)

target_link_libraries(MakeASoundCore PUBLIC Miro)
target_include_directories(MakeASoundCore PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundCore PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundCore)

# MakeASound, the device library over the core.
if (MAKEASOUND_BUILD_DEVICES)
    include(${CMAKE_CURRENT_LIST_DIR}/Devices.cmake)
else ()
    message(STATUS "MakeASound skipped (MAKEASOUND_BUILD_DEVICES is off): core only")
endif ()

# The optional DSP library, always built and linked only by what asks for it.
add_subdirectory(MakeASound/DSP)

# The plugin core and its drawing tiers live in their own directory.
if (MAKEASOUND_BUILD_PLUGIN)
    add_subdirectory(MakeASound/Plugin)
endif ()

# Hosting third-party plugins: beside the plugin core rather than inside it, since
# Plugin/Host is the plugin's own view of a host.
if (MAKEASOUND_BUILD_HOSTING AND NOT MAKEASOUND_DESCRIBE_BUILD)
    if (MAKEASOUND_BUILD_PLUGIN)
        add_subdirectory(MakeASound/Hosting)
    else ()
        message(STATUS "MakeASoundHosting skipped (needs MAKEASOUND_BUILD_PLUGIN)")
    endif ()
endif ()
