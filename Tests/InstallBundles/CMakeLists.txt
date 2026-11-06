# makeasound_add_plugin's copy of each bundle into the user's plug-in folder,
# checked at the CMake level in script mode (Install.cmake), so on every platform:
#
#   CMake/InstallBundles/helper      _makeasound_install_bundle, which every
#                                    format's install goes through, adds its
#                                    command only with install_bundles on
#   CMake/InstallBundles/aaxWindows  the Windows AAX honours NO_INSTALL, and
#                                    installs into Avid's folder otherwise
set(plugin_cmake "${PROJECT_SOURCE_DIR}/CMake/MakeASoundPlugin.cmake")

add_test(NAME CMake/InstallBundles/helper
        COMMAND ${CMAKE_COMMAND} -D "PLUGIN_CMAKE=${plugin_cmake}" -D CASE=helper
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Install.cmake")

add_test(NAME CMake/InstallBundles/aaxWindows
        COMMAND ${CMAKE_COMMAND} -D "PLUGIN_CMAKE=${plugin_cmake}" -D CASE=aaxWindows
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Install.cmake")
