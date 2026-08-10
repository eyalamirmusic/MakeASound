set(CMAKE_CXX_STANDARD 20)
set(CMAKE_FOLDER Tests)

find_package(NanoTest REQUIRED)

# NanoTest registers one ctest case per nano::test() and re-runs the binary with
# --test <name>, so `ctest` lists each case individually. Link MakeASound so the
# suite can grow to cover the whole public surface, not just the header-only
# pieces.
nano_add_executable(MakeASoundTests
        SOURCES
        SPSCQueueTests.cpp
        BufferTests.cpp
        DeviceInfoTests.cpp
        DeviceManagerTests.cpp
        TARGETS MakeASound)
