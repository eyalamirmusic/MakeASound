cmake_minimum_required(VERSION 3.16)
project(MakeASound)

set(CMAKE_CXX_STANDARD 17)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

find_package(RTAudio REQUIRED)

add_library(MakeASound INTERFACE)
target_link_libraries(MakeASound INTERFACE rtaudio)
target_include_directories(MakeASound INTERFACE Include)

add_executable(Example Example/Example.cpp)
target_link_libraries(Example PRIVATE MakeASound)