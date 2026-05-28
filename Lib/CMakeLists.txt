add_library(MakeASound STATIC
        MakeASound/DeviceInfo/DeviceInfo.cpp
        MakeASound/DeviceManager/DeviceManager.cpp
        MakeASound/MiniAudio/MiniAudio-Backend.cpp
        MakeASound/MiniAudio/MiniAudioDeviceManager.cpp
        MakeASound/MidiInfo/MidiInfo.cpp
        MakeASound/MidiManager/MidiManager.cpp
        MakeASound/MidiBlockSync/MidiBlockSync.cpp
        MakeASound/MIDI/MIDI.cpp
        MakeASound/RTMidi/RTMidi-Backend.cpp
        MakeASound/RTMidi/RTMidiManager.cpp
        MakeASound/UI/Dropdown.cpp
        MakeASound/UI/UIDeviceManager.cpp
        MakeASound/UI/UIMidiManager.cpp)

if(APPLE)
    target_sources(MakeASound PRIVATE MakeASound/BlockSizes/BlockSizes.mm)
    target_link_libraries(MakeASound PRIVATE "-framework CoreAudio")
else()
    target_sources(MakeASound PRIVATE MakeASound/BlockSizes/BlockSizes.cpp)
endif()

target_link_libraries(MakeASound PUBLIC Miro PRIVATE miniaudio rtmidi)
target_include_directories(MakeASound PUBLIC ${CMAKE_CURRENT_LIST_DIR})

set_makeasound_warnings(MakeASound)
