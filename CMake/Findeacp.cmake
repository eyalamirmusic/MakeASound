#including CPM.cmake, a package manager:
#https://github.com/TheLartians/CPM.cmake
include(CPM)

#Fetching eacp from git
#IF you want to instead point it to a local version, you can invoke CMake with
#-D CPM_eacp_SOURCE="Path_To_eacp"
set(EACP_WEBVIEW_VITE_BUILD ON)
CPMAddPackage("gh:eyalamirmusic/eacp#main")

if(APPLE AND NOT IOS AND eacp_SOURCE_DIR)
    set(EACP_MACOS_PLIST
            "${eacp_SOURCE_DIR}/CMake/macOSBundleInfo.plist.in"
            CACHE INTERNAL "")
endif()
