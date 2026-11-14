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
option(MAKEASOUND_UNITY_BUILD_TESTS "Compile the test executables as unity builds" OFF)
option(MAKEASOUND_CI_BUILD
       "Mirror the flag set CI builds with: unity builds here and in every dependency, eacp's own CI set" OFF)

# A cold build of the plugin core compiles all of eacp and the VST3 SDK, so CI
# compiles everything as unity builds, the test executables included, under
# ccache (see ci.yml). Off by default: a unity build rebuilds a whole chunk per
# edit, the wrong trade for an edit-compile loop, and a test file is what an
# edit-compile loop edits most. Cache entries, because the dependencies read them
# when CPM configures them.
if (MAKEASOUND_CI_BUILD)
    set(MAKEASOUND_UNITY_BUILD ON CACHE BOOL
            "Enable unity (jumbo) build for the MakeASound library" FORCE)
    set(MAKEASOUND_UNITY_BUILD_TESTS ON CACHE BOOL
            "Compile the test executables as unity builds" FORCE)
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
# account means wraptool's default, the last one that sync saw. On Windows wraptool
# has signtool add the Authenticode signature: through Microsoft Artifact Signing
# when its dlib and metadata.json are both set, else with the certificate whose
# SHA-1 thumbprint is the identity. The default wraptool and signtool are the newest
# installed. MAKEASOUND_PACE_REQUIRED fails the configure when a plugin with an
# AAX_WCGUID cannot be signed, and the build when wraptool fails or runs longer
# than MAKEASOUND_PACE_TIMEOUT seconds.
if (WIN32)
    set(program_files_x86 "$ENV{ProgramFiles\(x86\)}")

    if (NOT program_files_x86)
        set(program_files_x86 "C:/Program Files (x86)")
    endif ()

    file(TO_CMAKE_PATH "${program_files_x86}" program_files_x86)
    set(program_files "$ENV{ProgramFiles}")

    if (NOT program_files)
        set(program_files "C:/Program Files")
    endif ()

    file(TO_CMAKE_PATH "${program_files}" program_files)
    set(pace_wraptool_default
        "${program_files}/PACEAntiPiracy/Eden/Fusion/Versions/6/bin/wraptool.exe")
    set(pace_signtool_default
        "${program_files_x86}/Windows Kits/10/bin/10.0.26100.0/x64/signtool.exe")

    if (NOT MAKEASOUND_PACE_WRAPTOOL)
        file(GLOB pace_wraptools
             "${program_files_x86}/PACEAntiPiracy/Eden/Fusion/Versions/*/wraptool.exe")

        if (pace_wraptools)
            list(SORT pace_wraptools COMPARE NATURAL ORDER DESCENDING)
            list(GET pace_wraptools 0 pace_wraptool_default)
        endif ()
    endif ()

    if (NOT MAKEASOUND_PACE_SIGNTOOL)
        file(GLOB pace_signtools
             "${program_files_x86}/Windows Kits/10/bin/10.*/x64/signtool.exe")

        if (pace_signtools)
            list(SORT pace_signtools COMPARE NATURAL ORDER DESCENDING)
            list(GET pace_signtools 0 pace_signtool_default)
        endif ()
    endif ()

    set(MAKEASOUND_PACE_SIGNTOOL "${pace_signtool_default}" CACHE FILEPATH
        "The signtool wraptool runs (10.0.22621.755 or newer for Artifact Signing)")
    set(MAKEASOUND_PACE_SIGNING_DLIB "" CACHE FILEPATH
        "Artifact Signing's Azure.CodeSigning.Dlib.dll (x64), which signtool loads")
    set(MAKEASOUND_PACE_SIGNING_METADATA "" CACHE FILEPATH
        "Artifact Signing's metadata.json: endpoint, account and certificate profile")
else ()
    set(pace_wraptool_default "/Applications/PACEAntiPiracy/Eden/Fusion/Current/bin/wraptool")
endif ()

set(MAKEASOUND_PACE_WRAPTOOL "${pace_wraptool_default}"
    CACHE FILEPATH "PACE's wraptool, used to sign AAX bundles")
set(MAKEASOUND_PACE_ACCOUNT "" CACHE STRING
    "The iLok account that signs AAX bundles (empty: wraptool's default)")
set(MAKEASOUND_PACE_SIGN_ID "" CACHE STRING
    "The identity wraptool signs AAX bundles with: the Developer ID Application on macOS, the certificate's SHA-1 thumbprint on Windows")
option(MAKEASOUND_PACE_REQUIRED
       "Fail when an AAX bundle with an AAX_WCGUID cannot be PACE signed" OFF)
set(MAKEASOUND_PACE_TIMEOUT 600 CACHE STRING
    "Seconds each wraptool run may take before it is stopped as failed (0: no limit)")

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
