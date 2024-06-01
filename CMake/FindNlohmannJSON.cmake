#including CPM.cmake, a package manager:
#https://github.com/TheLartians/CPM.cmake
include(CPM)

#Fetching RTAudio from git
#IF you want to instead point it to a local version, you can invoke CMake with
#-D CPM_RTAudio_SOURCE="Path_To_RTAudio"
CPMAddPackage("gh:nlohmann/json#master")