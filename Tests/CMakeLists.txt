set(CMAKE_CXX_STANDARD 20)
set(CMAKE_FOLDER Tests)

find_package(NanoTest REQUIRED)

set(MAKEASOUND_TEST_SOURCES
        SPSCQueueTests.cpp
        BufferTests.cpp
        DeviceInfoTests.cpp
        DeviceManagerTests.cpp)

set(MAKEASOUND_TEST_TARGETS MakeASound)

# The allocation suites interpose malloc/new through dlsym(RTLD_NEXT, ...), which
# only exists on Apple and Linux. Elsewhere the checker degrades to a stub, so the
# suites are left out of the build rather than compiled into something that reports
# zero allocations because nothing was watching.
#
# Test-only: linking AllocationsChecker compiles the interposers into this
# executable and nothing else. See CMake/FindScopedMemoryAllocations.cmake.
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
