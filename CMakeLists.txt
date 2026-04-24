cmake_minimum_required(VERSION 3.21)
project(MakeASound)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

find_package(RTAudio REQUIRED)
find_package(Miro REQUIRED)

add_subdirectory(Lib)

if (PROJECT_IS_TOP_LEVEL)
    add_subdirectory(Example)
endif ()