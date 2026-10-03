#including CPM.cmake, a package manager:
#https://github.com/TheLartians/CPM.cmake
include(CPM)

#Fetching eacp from git
#IF you want to instead point it to a local version, you can invoke CMake with
#-D CPM_eacp_SOURCE="Path_To_eacp"
set(EACP_WEBVIEW_VITE_BUILD ON)
CPMAddPackage("gh:eyalamirmusic/eacp#develop")
