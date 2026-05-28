# Fetch miniaudio's single header via CPM and build it as a STATIC library
# from a one-line implementation TU. miniaudio handles backend selection
# internally via #ifdefs, so we only need to link the per-platform system
# frameworks/libraries it depends on.
#
# To point at a local checkout instead of fetching, configure with
# -DCPM_miniaudio_SOURCE="Path_To_miniaudio".

include(CPM)

message(STATUS "Fetching miniaudio (first configure only)...")
CPMAddPackage(
        NAME miniaudio
        URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.zip
        DOWNLOAD_ONLY YES
        DOWNLOAD_NO_PROGRESS YES
        QUIET
)

set(MINIAUDIO_IMPL_DIR ${CMAKE_CURRENT_BINARY_DIR}/miniaudio_impl)
file(MAKE_DIRECTORY ${MINIAUDIO_IMPL_DIR})

set(MINIAUDIO_IMPL_FILE ${MINIAUDIO_IMPL_DIR}/miniaudio.c)
file(WRITE ${MINIAUDIO_IMPL_FILE}
        "#define MINIAUDIO_IMPLEMENTATION\n#include \"miniaudio.h\"\n")

add_library(miniaudio STATIC ${MINIAUDIO_IMPL_FILE})
target_include_directories(miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})

if (APPLE)
    target_link_libraries(miniaudio PRIVATE
            "-framework CoreAudio"
            "-framework CoreFoundation"
            "-framework AudioToolbox")
elseif (WIN32)
    target_link_libraries(miniaudio PRIVATE ole32)
elseif (UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif ()

# miniaudio's amalgamated implementation triggers a handful of pedantic
# warnings (mostly around backend code paths we don't touch). Silence them
# so our own build output stays clean.
if (MSVC)
    target_compile_options(miniaudio PRIVATE /w)
else ()
    target_compile_options(miniaudio PRIVATE -w)
endif ()
