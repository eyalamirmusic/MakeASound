# cmake -D SOURCE=<bundle> -D DEST_DIR=<folder> -P InstallPluginBundle.cmake
#
# Copies a built plug-in bundle into the user's plug-in folder, replacing the one
# there. Best effort: a folder that cannot be written is a warning, never a failed
# build, because the bundle in the build tree is already complete.

if (NOT SOURCE OR NOT DEST_DIR)
    message(FATAL_ERROR "InstallPluginBundle: SOURCE and DEST_DIR are required")
endif ()

get_filename_component(bundle_name "${SOURCE}" NAME)
set(destination "${DEST_DIR}/${bundle_name}")

file(MAKE_DIRECTORY "${DEST_DIR}")
file(REMOVE_RECURSE "${destination}")

execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SOURCE}" "${destination}"
        RESULT_VARIABLE result)

if (result EQUAL 0 AND EXISTS "${destination}")
    message(STATUS "Installed ${bundle_name} into ${DEST_DIR}")
else ()
    message(WARNING "Could not install ${bundle_name} into ${DEST_DIR}")
endif ()
