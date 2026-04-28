add_library(MakeASound STATIC
        MakeASound/DeviceManager/DeviceManager.cpp
        MakeASound/RTAudio/RTAudio-Backend.cpp
        MakeASound/RTAudio/RTAudioDeviceManager.cpp
        MakeASound/MidiManager/MidiManager.cpp
        MakeASound/RTMidi/RTMidi-Backend.cpp
        MakeASound/RTMidi/RTMidiManager.cpp)

target_link_libraries(MakeASound PUBLIC Miro PRIVATE rtaudio rtmidi ea_data_structures)
target_include_directories(MakeASound PUBLIC ${CMAKE_CURRENT_LIST_DIR})
