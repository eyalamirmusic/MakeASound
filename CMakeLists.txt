cmake_minimum_required(VERSION 3.31)
project(MakeASound)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

# Nothing here or in a dependency uses C++ modules, and a source CMake scans for
# them is left out of its target's unity build: with MSVC and clang-cl the unity
# builds below were skipped wholesale, and a Windows runner compiled every TU.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(MAKEASOUND_UNITY_BUILD "Enable unity (jumbo) build for the MakeASound library" OFF)
option(MAKEASOUND_CI_BUILD
       "Mirror the flag set CI builds with: unity builds here and in every dependency, eacp's own CI set" OFF)

# A cold build of the plugin core compiles all of eacp and the VST3 SDK, so CI
# compiles everything as unity builds with eacp's precompiled header. Off by
# default: a unity build rebuilds a whole chunk per edit, the wrong trade for an
# edit-compile loop. Cache entries, because the dependencies read them when CPM
# configures them.
if (MAKEASOUND_CI_BUILD)
    set(MAKEASOUND_UNITY_BUILD ON CACHE BOOL
            "Enable unity (jumbo) build for the MakeASound library" FORCE)
    set(EACP_CI_BUILD ON CACHE BOOL "Mirror the exact flag set used by CI builds" FORCE)
    set(MIRO_UNITY_BUILD ON CACHE BOOL "Enable CMake unity build for the Miro library" FORCE)
    set(ESIMD_UNITY_BUILD ON CACHE BOOL "Compile ESIMD as a CMake unity build" FORCE)
endif ()
option(MAKEASOUND_BUILD_APPS "Build the example/demo apps (top-level builds only)" ON)
option(MAKEASOUND_BUILD_TESTS "Build the unit tests (top-level builds only)" ON)
option(MAKEASOUND_BUILD_PLUGIN "Build MakeASoundPlugin, the SDK-free plugin core (needs eacp)"
       ${PROJECT_IS_TOP_LEVEL})
option(MAKEASOUND_BUILD_EXAMPLES "Build the example plugins in Plugins/ (top-level builds only)" ON)
option(MAKEASOUND_INSTALL_PLUGINS "Copy each built plug-in bundle into the user's plug-in folder" OFF)

# A .vst3 is a shared module on Linux, and every static archive linked into it,
# ours and the dependencies' alike, has to be position independent for that.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

# Every target of ours: the warnings, and link-time optimisation in Release to
# match eacp, whose Release archives are LTO bitcode under Clang that a link
# without -flto cannot read (GNU ld: "file format not recognized").
function(set_makeasound_target_settings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic)
    endif ()

    set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endfunction()

find_package(Miniaudio REQUIRED)
find_package(Miro REQUIRED)

if (PROJECT_IS_TOP_LEVEL)
    include(ExternalFolders)
    cmake_language(DEFER CALL makeasound_group_external_targets)
endif ()

# The vendored VST3 SDK and, on macOS, the fetched AudioUnitSDK; see ThirdParty/README.md.
if (MAKEASOUND_BUILD_PLUGIN)
    add_subdirectory(ThirdParty)
endif ()

add_subdirectory(Lib)

if (PROJECT_IS_TOP_LEVEL AND MAKEASOUND_BUILD_APPS)
    add_subdirectory(Apps)
endif ()

if (PROJECT_IS_TOP_LEVEL AND MAKEASOUND_BUILD_EXAMPLES AND MAKEASOUND_BUILD_PLUGIN)
    add_subdirectory(Plugins)
    add_subdirectory(Tools)
endif ()

if (PROJECT_IS_TOP_LEVEL AND MAKEASOUND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif ()
