add_library(MakeASound STATIC MakeASound/MakeASound.cpp)
target_link_libraries(MakeASound PUBLIC Miro PRIVATE rtaudio)
target_include_directories(MakeASound PUBLIC ${CMAKE_CURRENT_LIST_DIR})