# MakeASound: the device library, audio over miniaudio and MIDI over the platform's
# own API, on top of MakeASoundCore. Included from Lib/CMakeLists.txt only when
# MAKEASOUND_BUILD_DEVICES is on.

add_library(MakeASound STATIC
        MakeASound/Devices/DeviceInfo.cpp
        MakeASound/Devices/DeviceManager.cpp
        MakeASound/Devices/AudioSession.cpp
        MakeASound/Devices/Engine.cpp
        MakeASound/MiniAudio/MiniAudio-Backend.cpp
        MakeASound/MiniAudio/MiniAudioDeviceManager.cpp
        MakeASound/MIDI/MidiBackend.cpp
        MakeASound/MIDI/MidiInputPipe.cpp
        MakeASound/MIDI/MidiPortRegistry.cpp
        MakeASound/MIDI/MidiManager.cpp
        MakeASound/MIDI/MidiBlockSync.cpp
        MakeASound/UI/Dropdown.cpp
        MakeASound/UI/UIDeviceManager.cpp
        MakeASound/UI/UIMidiManager.cpp)

# One device-query implementation per platform: the CoreAudio HAL is macOS-only, and
# iOS answers the same questions from AVAudioSession instead.
if(IOS)
    target_sources(MakeASound PRIVATE
            MakeASound/Devices/DeviceQueriesIOS.mm
            MakeASound/Devices/AudioSession-iOS.mm)
    target_link_libraries(MakeASound PRIVATE "-framework AVFoundation")
elseif(APPLE)
    target_sources(MakeASound PRIVATE
            MakeASound/Devices/DeviceQueries.mm
            MakeASound/Devices/AudioSession-Default.cpp)
    target_link_libraries(MakeASound PRIVATE "-framework CoreAudio")
else()
    target_sources(MakeASound PRIVATE
            MakeASound/Devices/DeviceQueries.cpp
            MakeASound/Devices/AudioSession-Default.cpp)
endif()

# One MIDI backend per platform, picked the same way: Core MIDI on macOS and iOS,
# WinMM on Windows, the ALSA sequencer on Linux, and the null backend on anything
# else (Android, Emscripten) or when MAKEASOUND_MIDI_BACKEND is Null. The null
# backend's class is built everywhere, so its tests run on every platform; only its
# factory decides whether it is the one the façade gets.
target_sources(MakeASound PRIVATE MakeASound/NullMIDI/NullMidiManager.cpp)

if (MAKEASOUND_MIDI_BACKEND STREQUAL "Null")
    set(makeasound_midi_backend Null)
elseif (NOT MAKEASOUND_MIDI_BACKEND STREQUAL "Auto")
    message(FATAL_ERROR
            "MAKEASOUND_MIDI_BACKEND is '${MAKEASOUND_MIDI_BACKEND}'; use Auto or Null")
elseif (APPLE)
    set(makeasound_midi_backend CoreMIDI)
elseif (WIN32)
    set(makeasound_midi_backend WinMIDI)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(makeasound_midi_backend ALSA)
else ()
    set(makeasound_midi_backend Null)
endif ()

# The tests pick their backend-specific suites from this.
set_property(GLOBAL PROPERTY MAKEASOUND_MIDI_BACKEND_IN_USE ${makeasound_midi_backend})
message(STATUS "MakeASound MIDI backend: ${makeasound_midi_backend}")

if (makeasound_midi_backend STREQUAL "CoreMIDI")
    target_sources(MakeASound PRIVATE
            MakeASound/CoreMIDI/CoreMIDI-Backend.cpp
            MakeASound/CoreMIDI/CoreMIDIManager.cpp
            MakeASound/CoreMIDI/CoreMIDI-Factory.cpp)
    target_link_libraries(MakeASound PRIVATE
            "-framework CoreMIDI"
            "-framework CoreFoundation")
elseif (makeasound_midi_backend STREQUAL "WinMIDI")
    target_sources(MakeASound PRIVATE
            MakeASound/WinMIDI/WinMIDI-Backend.cpp
            MakeASound/WinMIDI/WinMidiManager.cpp
            MakeASound/WinMIDI/WinMIDI-Factory.cpp)

    # On the target rather than only in the header: a unity build concatenates
    # these with every other TU, and windows.h getting in first without them is
    # what would leave min/max as macros for the rest of the blob.
    target_compile_definitions(MakeASound PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(MakeASound PRIVATE winmm)
elseif (makeasound_midi_backend STREQUAL "ALSA")
    find_package(ALSA REQUIRED)
    find_package(Threads REQUIRED)

    target_sources(MakeASound PRIVATE
            MakeASound/ALSA/ALSA-Backend.cpp
            MakeASound/ALSA/ALSAMidiManager.cpp
            MakeASound/ALSA/ALSA-Factory.cpp)
    target_link_libraries(MakeASound PRIVATE ALSA::ALSA Threads::Threads)
else ()
    target_sources(MakeASound PRIVATE MakeASound/NullMIDI/NullMIDI-Factory.cpp)
endif ()

# The core PUBLIC, so everything that linked MakeASound for the vocabulary still
# gets it from here.
target_link_libraries(MakeASound PUBLIC MakeASoundCore PRIVATE miniaudio)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASound PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASound)
