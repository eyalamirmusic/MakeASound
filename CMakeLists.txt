cmake_minimum_required(VERSION 3.31)
project(MakeASound)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

# Before anything is fetched, so the dependencies configure under it too.
include(MakeASoundDefaults)

if (PROJECT_IS_TOP_LEVEL)
    makeasound_default_setup()
endif ()

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
# compiles everything as unity builds, under ccache (see ci.yml). Off by
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
# Off for a consumer that wants only MakeASoundCore (the audio and MIDI vocabulary,
# the processing types): no MakeASound target then, so no miniaudio fetch, no
# platform audio or MIDI framework, and nothing that needs devices (the apps, the
# standalone format, the main test suite).
option(MAKEASOUND_BUILD_DEVICES
       "Build MakeASound, the device library (audio and MIDI I/O; fetches miniaudio)" ON)
# What a top-level build builds beyond the libraries: the apps, the example plugins,
# the tools and the tests, plus the plugin core and hosting by default. A consumer
# gets none of it, but a workspace that adds this tree as a subdirectory to build it
# against local checkouts wants all of it, and cannot make PROJECT_IS_TOP_LEVEL true,
# so it sets this instead.
option(MAKEASOUND_BUILD_ALL
       "Build the apps, examples, tools and tests too (default: only as the top-level project)"
       ${PROJECT_IS_TOP_LEVEL})
option(MAKEASOUND_BUILD_APPS "Build the example/demo apps (with MAKEASOUND_BUILD_ALL only)" ON)
option(MAKEASOUND_BUILD_TESTS "Build the unit tests (with MAKEASOUND_BUILD_ALL only)" ON)
option(MAKEASOUND_BUILD_PLUGIN "Build MakeASoundPlugin, the SDK-free plugin core (needs eacp)"
       ${MAKEASOUND_BUILD_ALL})
option(MAKEASOUND_BUILD_HOSTING
       "Build MakeASoundHosting, scanning and loading third-party plugins (needs MAKEASOUND_BUILD_PLUGIN)"
       ${MAKEASOUND_BUILD_ALL})
option(MAKEASOUND_BUILD_EXAMPLES
       "Build the example plugins in Plugins/ (with MAKEASOUND_BUILD_ALL only)" ON)
option(MAKEASOUND_INSTALL_PLUGINS "Copy each built plug-in bundle into the user's plug-in folder" ON)
option(MAKEASOUND_BUILD_AAX
       "Fetch Avid's AAX SDK and build the AAX format (macOS, Windows x64; needs MAKEASOUND_BUILD_PLUGIN)" OFF)

# PACE signing of each AAX bundle after it builds, on when wraptool is there, the
# identity is set and the plugin names its AAX_WCGUID. No password is passed:
# wraptool reads it from the keychain, where one sync with it saved it, and an empty
# account means wraptool's default, the last one that sync saw.
set(MAKEASOUND_PACE_WRAPTOOL "/Applications/PACEAntiPiracy/Eden/Fusion/Current/bin/wraptool"
    CACHE FILEPATH "PACE's wraptool, used to sign AAX bundles")
set(MAKEASOUND_PACE_ACCOUNT "" CACHE STRING
    "The iLok account that signs AAX bundles (empty: wraptool's default)")
set(MAKEASOUND_PACE_SIGN_ID "" CACHE STRING
    "The Developer ID Application identity wraptool signs AAX bundles with")

# Set only by an iOS tree on the host sub-build it runs for the AUv3 plists (see
# CMake/MakeASoundPlugin.cmake): this same project for macOS, where
# makeasound_add_plugin builds each core and its <Name>-Describe tool and nothing
# else, and no format, app, tool, test or SDK is configured.
option(MAKEASOUND_DESCRIBE_BUILD "Internal: the host build of the AUv3 plist tools" OFF)

# A .vst3 is a shared module on Linux, and every static archive linked into it,
# ours and the dependencies' alike, has to be position independent for that.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
endif ()

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

if (MAKEASOUND_BUILD_DEVICES)
    find_package(Miniaudio REQUIRED)
endif ()

find_package(Miro REQUIRED)

if (PROJECT_IS_TOP_LEVEL)
    include(ExternalFolders)
    cmake_language(DEFER CALL makeasound_group_external_targets)
endif ()

# The vendored VST3 SDK and, on macOS, the fetched AudioUnitSDK; see ThirdParty/README.md.
if (MAKEASOUND_BUILD_PLUGIN AND NOT MAKEASOUND_DESCRIBE_BUILD)
    add_subdirectory(ThirdParty)
endif ()

add_subdirectory(Lib)

if (MAKEASOUND_BUILD_ALL AND MAKEASOUND_BUILD_APPS AND MAKEASOUND_BUILD_DEVICES
        AND NOT MAKEASOUND_DESCRIBE_BUILD)
    add_subdirectory(Apps)
endif ()

if (MAKEASOUND_BUILD_ALL AND MAKEASOUND_BUILD_EXAMPLES AND MAKEASOUND_BUILD_PLUGIN)
    add_subdirectory(Plugins)

    if (NOT MAKEASOUND_DESCRIBE_BUILD)
        add_subdirectory(Tools)
    endif ()
endif ()

if (MAKEASOUND_BUILD_ALL AND MAKEASOUND_BUILD_TESTS AND NOT MAKEASOUND_DESCRIBE_BUILD)
    enable_testing()
    add_subdirectory(Tests)
endif ()
