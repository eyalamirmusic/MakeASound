cmake_minimum_required(VERSION 3.31)
project(MakeASound)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

option(MAKEASOUND_UNITY_BUILD "Enable unity (jumbo) build for the MakeASound library" OFF)
option(MAKEASOUND_BUILD_APPS "Build the example/demo apps (top-level builds only)" ON)

if (APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

function(set_makeasound_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic)
    endif ()
endfunction()

find_package(Miniaudio REQUIRED)
find_package(RTMidi REQUIRED)
find_package(Miro REQUIRED)

add_subdirectory(Lib)

if (PROJECT_IS_TOP_LEVEL AND MAKEASOUND_BUILD_APPS)
    add_subdirectory(Apps)
endif ()
