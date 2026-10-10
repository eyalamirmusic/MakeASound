cmake_minimum_required(VERSION 3.31)
project(MakeASound)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

option(MAKEASOUND_UNITY_BUILD "Enable unity (jumbo) build for the MakeASound library" OFF)
option(MAKEASOUND_BUILD_APPS "Build the example/demo apps (top-level builds only)" ON)
option(MAKEASOUND_BUILD_TESTS "Build the unit tests (top-level builds only)" ON)
option(MAKEASOUND_BUILD_PLUGIN "Build MakeASoundPlugin, the SDK-free plugin core (needs eacp)"
       ${PROJECT_IS_TOP_LEVEL})
option(MAKEASOUND_BUILD_EXAMPLES "Build the example plugins in Plugins/ (top-level builds only)" ON)

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
find_package(Miro REQUIRED)

if (PROJECT_IS_TOP_LEVEL)
    include(ExternalFolders)
    cmake_language(DEFER CALL makeasound_group_external_targets)
endif ()

# The vendored VST3 SDK, for the VST3 format; see ThirdParty/README.md.
if (MAKEASOUND_BUILD_PLUGIN)
    add_subdirectory(ThirdParty)
endif ()

add_subdirectory(Lib)

if (PROJECT_IS_TOP_LEVEL AND MAKEASOUND_BUILD_APPS)
    add_subdirectory(Apps)
endif ()

if (PROJECT_IS_TOP_LEVEL AND MAKEASOUND_BUILD_EXAMPLES AND MAKEASOUND_BUILD_PLUGIN)
    add_subdirectory(Plugins)
endif ()

if (PROJECT_IS_TOP_LEVEL AND MAKEASOUND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif ()
