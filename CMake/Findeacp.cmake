#including CPM.cmake, a package manager:
#https://github.com/TheLartians/CPM.cmake
include(CPM)

# eacp's WebView module is off by default when MakeASound is the top-level
# project: nothing here uses it. A cache default, not FORCE, so
# -DEACP_BUILD_WEBVIEW=ON brings it back for a plugin whose editor is a WebView.
# As a dependency (Plug, a workspace root) eacp's options are the consumer's.
# A cache entry, not a normal variable: CPM shadows this module with one of its
# own after the first add, so only the first caller's scope would see a plain set.
if (PROJECT_IS_TOP_LEVEL AND PROJECT_NAME STREQUAL "MakeASound")
    set(EACP_BUILD_WEBVIEW OFF CACHE BOOL "Build the WebView module")
endif ()

#Fetching eacp from git
#IF you want to instead point it to a local version, you can invoke CMake with
#-D CPM_eacp_SOURCE="Path_To_eacp"
CPMAddPackage("gh:eyalamirmusic/eacp#develop")
