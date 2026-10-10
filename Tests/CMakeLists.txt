set(CMAKE_CXX_STANDARD 20)
set(CMAKE_FOLDER Tests)

find_package(NanoTest REQUIRED)

set(MAKEASOUND_TEST_SOURCES
        SPSCQueueTests.cpp
        BufferTests.cpp
        DeviceInfoTests.cpp
        DeviceManagerTests.cpp
        MidiParserTests.cpp
        MidiManagerTests.cpp)

set(MAKEASOUND_TEST_TARGETS MakeASound)

# The ALSA backend's conversions are the one half of a platform backend that can
# be measured without the platform: alsa-lib turns events into bytes and back
# with no sequencer open, so these run on a container where the loopback below
# has nothing to open.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(ALSA REQUIRED)

    list(APPEND MAKEASOUND_TEST_SOURCES ALSABackendTests.cpp)
    list(APPEND MAKEASOUND_TEST_TARGETS ALSA::ALSA)
endif ()

# The allocation suites interpose malloc/new through dlsym(RTLD_NEXT, ...), which
# only exists on Apple and Linux. Elsewhere the checker degrades to a stub, so the
# suites are left out of the build rather than compiled into something that reports
# zero allocations because nothing was watching.
#
# Test-only: linking AllocationsChecker compiles the interposers into this
# executable and nothing else. See CMake/FindScopedMemoryAllocations.cmake.
#
# No CI runner has MIDI hardware either, so the input path is measured through a
# virtual-port loopback: our own output sending into our own virtual input, which
# is a real platform round trip on macOS, iOS and Linux. Windows has no virtual
# ports, so there its input path is covered by the parser tests alone and every
# loopback case returns without asserting. A Linux box with no sequencer -
# a container with no /dev/snd/seq - is in the same position, and skips the same
# way rather than failing.
if (APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(ScopedMemoryAllocations REQUIRED)

    list(APPEND MAKEASOUND_TEST_SOURCES
            AllocationTests.cpp
            RealtimeThreadAllocationTests.cpp)

    list(APPEND MAKEASOUND_TEST_TARGETS AllocationsChecker)
endif ()

# NanoTest registers one ctest case per nano::test() and re-runs the binary with
# --test <name>, so `ctest` lists each case individually. Link MakeASound so the
# suite can grow to cover the whole public surface, not just the header-only
# pieces.
nano_add_executable(MakeASoundTests
        SOURCES ${MAKEASOUND_TEST_SOURCES}
        TARGETS ${MAKEASOUND_TEST_TARGETS})
