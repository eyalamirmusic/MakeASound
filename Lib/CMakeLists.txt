add_library(MakeASound STATIC
        MakeASound/Devices/DeviceInfo.cpp
        MakeASound/Devices/DeviceManager.cpp
        MakeASound/Devices/AudioSession.cpp
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

# One MIDI backend per platform, picked the same way: Core MIDI is the library's
# own on macOS and iOS, everything else still goes through RtMidi.
if(APPLE)
    target_sources(MakeASound PRIVATE
            MakeASound/CoreMIDI/CoreMIDI-Backend.cpp
            MakeASound/CoreMIDI/CoreMIDIManager.cpp
            MakeASound/CoreMIDI/CoreMIDI-Factory.cpp)
    target_link_libraries(MakeASound PRIVATE
            "-framework CoreMIDI"
            "-framework CoreFoundation")
else()
    target_sources(MakeASound PRIVATE
            MakeASound/RTMidi/RTMidi-Backend.cpp
            MakeASound/RTMidi/RTMidiManager.cpp
            MakeASound/RTMidi/RTMidi-Factory.cpp)
    target_link_libraries(MakeASound PRIVATE rtmidi)
endif()

target_link_libraries(MakeASound PUBLIC Miro PRIVATE miniaudio)
target_include_directories(MakeASound PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASound PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_warnings(MakeASound)
