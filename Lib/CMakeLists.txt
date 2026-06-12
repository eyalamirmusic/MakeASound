add_library(MakeASound STATIC
        MakeASound/Audio/Channel.cpp
        MakeASound/Audio/Buffer.cpp
        MakeASound/Devices/DeviceInfo.cpp
        MakeASound/Devices/DeviceManager.cpp
        MakeASound/MiniAudio/MiniAudio-Backend.cpp
        MakeASound/MiniAudio/MiniAudioDeviceManager.cpp
        MakeASound/MIDI/MidiInfo.cpp
        MakeASound/MIDI/MidiManager.cpp
        MakeASound/MIDI/MidiBlockSync.cpp
        MakeASound/MIDI/MIDI.cpp
        MakeASound/RTMidi/RTMidi-Backend.cpp
        MakeASound/RTMidi/RTMidiManager.cpp
        MakeASound/UI/Dropdown.cpp
        MakeASound/UI/UIDeviceManager.cpp
        MakeASound/UI/UIMidiManager.cpp)

if(APPLE)
    target_sources(MakeASound PRIVATE MakeASound/Devices/BlockSizes.mm)
    target_link_libraries(MakeASound PRIVATE "-framework CoreAudio")
else()
    target_sources(MakeASound PRIVATE MakeASound/Devices/BlockSizes.cpp)
endif()

target_link_libraries(MakeASound PUBLIC Miro PRIVATE miniaudio rtmidi)
target_include_directories(MakeASound PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASound PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_warnings(MakeASound)
