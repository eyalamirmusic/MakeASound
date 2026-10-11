#including CPM.cmake, a package manager:
#https://github.com/TheLartians/CPM.cmake
include(CPM)

#Fetching eacp from git
#IF you want to instead point it to a local version, you can invoke CMake with
#-D CPM_eacp_SOURCE="Path_To_eacp"
# A cache entry, not a normal variable: CPM shadows this module with one of its
# own after the first add, so only the first caller's scope would see a plain set.
set(EACP_WEBVIEW_VITE_BUILD ON CACHE BOOL
        "Let CMake drive the Vite build of the embedded webview apps")
CPMAddPackage("gh:eyalamirmusic/eacp#develop")
