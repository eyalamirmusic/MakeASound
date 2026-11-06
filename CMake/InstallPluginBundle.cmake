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

# Through cmake -E, since file() stops the script on a folder it cannot write,
# which a system-wide one such as Avid's usually is without admin rights.
execute_process(
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DEST_DIR}"
        RESULT_VARIABLE created
        ERROR_QUIET)

execute_process(
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${destination}"
        RESULT_VARIABLE removed
        ERROR_QUIET)

execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SOURCE}" "${destination}"
        RESULT_VARIABLE result
        ERROR_QUIET)

if (created EQUAL 0 AND removed EQUAL 0 AND result EQUAL 0 AND EXISTS "${destination}")
    message(STATUS "Installed ${bundle_name} into ${DEST_DIR}")
else ()
    message(WARNING "Could not install ${bundle_name} into ${DEST_DIR} "
            "(a system folder needs write access for this user)")
endif ()
