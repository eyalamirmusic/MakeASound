cmake_minimum_required(VERSION 3.21)
project(MakeASound)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

find_package(RTAudio REQUIRED)
find_package(Miro REQUIRED)

# RtAudio's source has a few non-standard constructs (VLAs) that clang/gcc
# warn on. They're upstream and harmless — silence them so our own build
# output stays clean.
if(MSVC)
    target_compile_options(rtaudio PRIVATE /w)
else()
    target_compile_options(rtaudio PRIVATE -w)
endif()

add_library(MakeASound STATIC Lib/MakeASound/MakeASound.cpp)
target_link_libraries(MakeASound PUBLIC Miro PRIVATE rtaudio)
target_include_directories(MakeASound PUBLIC Lib)

add_executable(Example Example/Example.cpp)
target_link_libraries(Example PRIVATE MakeASound)

