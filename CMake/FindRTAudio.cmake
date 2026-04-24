#including CPM.cmake, a package manager:
#https://github.com/TheLartians/CPM.cmake
include(CPM)

#Fetching RTAudio from git
#IF you want to instead point it to a local version, you can invoke CMake with
#-D CPM_RTAudio_SOURCE="Path_To_RTAudio"
CPMAddPackage("gh:thestk/rtaudio#master")

# RtAudio's source has a few non-standard constructs (VLAs) that clang/gcc
# warn on. They're upstream and harmless — silence them so our own build
# output stays clean.
if(MSVC)
    target_compile_options(rtaudio PRIVATE /w)
else()
    target_compile_options(rtaudio PRIVATE -w)
endif()
