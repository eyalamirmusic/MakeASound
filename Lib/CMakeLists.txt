add_library(MakeASound STATIC
        MakeASound/DeviceManager/DeviceManager.cpp
        MakeASound/RTAudio/RTAudio-Backend.cpp
        MakeASound/RTAudio/RTAudioDeviceManager.cpp)

target_link_libraries(MakeASound PUBLIC Miro PRIVATE rtaudio)
target_include_directories(MakeASound PUBLIC ${CMAKE_CURRENT_LIST_DIR})
