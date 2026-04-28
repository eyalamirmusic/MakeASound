cmake_minimum_required(VERSION 3.31)
project(MakeASound)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

if (APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

find_package(RTAudio REQUIRED)
find_package(RTMidi REQUIRED)
find_package(Miro REQUIRED)
find_package(ea_data_structures REQUIRED)

add_subdirectory(Lib)

if (PROJECT_IS_TOP_LEVEL)
    find_package(eacp REQUIRED)

    add_subdirectory(Example)
    add_subdirectory(Demo)
endif ()
