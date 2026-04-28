#including CPM.cmake, a package manager:
#https://github.com/TheLartians/CPM.cmake
include(CPM)

#Fetching ea_data_structures from git
#IF you want to instead point it to a local version, you can invoke CMake with
#-D CPM_ea_data_structures_SOURCE="Path_To_cpp_data_structures"
CPMAddPackage("gh:eyalamirmusic/cpp_data_structures#main")
