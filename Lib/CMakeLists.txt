set(CMAKE_FOLDER Lib)

add_library(MakeASound STATIC
        MakeASound/Audio/Buffer.cpp
        MakeASound/Audio/ProcessContext.cpp
        MakeASound/Devices/DeviceInfo.cpp
        MakeASound/Devices/DeviceManager.cpp
        MakeASound/Devices/AudioSession.cpp
        MakeASound/Devices/Engine.cpp
        MakeASound/MiniAudio/MiniAudio-Backend.cpp
        MakeASound/MiniAudio/MiniAudioDeviceManager.cpp
        MakeASound/MIDI/MidiInfo.cpp
        MakeASound/MIDI/MidiBackend.cpp
        MakeASound/MIDI/MidiPortRegistry.cpp
        MakeASound/MIDI/MidiManager.cpp
        MakeASound/MIDI/MidiBlockSync.cpp
        MakeASound/MIDI/MidiParser.cpp
        MakeASound/MIDI/MIDI.cpp
        MakeASound/UI/Dropdown.cpp
        MakeASound/UI/UIDeviceManager.cpp
        MakeASound/UI/UIMidiManager.cpp)

# One device-query implementation per platform: the CoreAudio HAL is macOS-only, and
# iOS answers the same questions from AVAudioSession instead.
if(IOS)
    target_sources(MakeASound PRIVATE
            MakeASound/Devices/DeviceQueriesIOS.mm
            MakeASound/Devices/AudioSession-iOS.mm)
    target_link_libraries(MakeASound PRIVATE "-framework AVFoundation")
elseif(APPLE)
    target_sources(MakeASound PRIVATE
            MakeASound/Devices/DeviceQueries.mm
            MakeASound/Devices/AudioSession-Default.cpp)
    target_link_libraries(MakeASound PRIVATE "-framework CoreAudio")
else()
    target_sources(MakeASound PRIVATE
            MakeASound/Devices/DeviceQueries.cpp
            MakeASound/Devices/AudioSession-Default.cpp)
endif()

# One MIDI backend per platform, picked the same way: Core MIDI on macOS and iOS,
# WinMM on Windows, the ALSA sequencer on Linux. A platform with none of those has
# no MIDI, so it fails here rather than linking a library with a dead façade.
if(APPLE)
    target_sources(MakeASound PRIVATE
            MakeASound/CoreMIDI/CoreMIDI-Backend.cpp
            MakeASound/CoreMIDI/CoreMIDIManager.cpp
            MakeASound/CoreMIDI/CoreMIDI-Factory.cpp)
    target_link_libraries(MakeASound PRIVATE
            "-framework CoreMIDI"
            "-framework CoreFoundation")
elseif(WIN32)
    target_sources(MakeASound PRIVATE
            MakeASound/WinMIDI/WinMIDI-Backend.cpp
            MakeASound/WinMIDI/WinMidiManager.cpp
            MakeASound/WinMIDI/WinMIDI-Factory.cpp)

    # On the target rather than only in the header: a unity build concatenates
    # these with every other TU, and windows.h getting in first without them is
    # what would leave min/max as macros for the rest of the blob.
    target_compile_definitions(MakeASound PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(MakeASound PRIVATE winmm)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(ALSA REQUIRED)
    find_package(Threads REQUIRED)

    target_sources(MakeASound PRIVATE
            MakeASound/ALSA/ALSA-Backend.cpp
            MakeASound/ALSA/ALSAMidiManager.cpp
            MakeASound/ALSA/ALSA-Factory.cpp)
    target_link_libraries(MakeASound PRIVATE ALSA::ALSA Threads::Threads)
else()
    message(FATAL_ERROR "No MIDI backend for ${CMAKE_SYSTEM_NAME}")
endif()

target_link_libraries(MakeASound PUBLIC Miro PRIVATE miniaudio)
target_include_directories(MakeASound PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASound PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASound)

# The optional DSP library, always built and linked only by what asks for it.
add_subdirectory(MakeASound/DSP)

# The plugin core and its drawing tiers live in their own directory.
if (MAKEASOUND_BUILD_PLUGIN)
    add_subdirectory(MakeASound/Plugin)
endif ()
