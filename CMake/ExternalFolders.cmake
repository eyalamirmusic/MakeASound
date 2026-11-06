# Every target a dependency defines goes under the IDE folder External/<project>,
# keeping whatever folder the dependency gave it below that. A dependency's tree is
# the first directory on the way down whose project() is not ours, wherever CPM put
# it or a CPM_<Name>_SOURCE pointed, and a nested third-party project stays filed
# under the dependency that brought it. Deferred to the end of the top-level
# configure, when every find_package has run.
function(makeasound_group_external_targets)
    _makeasound_group_directory("${CMAKE_SOURCE_DIR}" "")
endfunction()

function(_makeasound_group_directory dir package)
    if (package STREQUAL "")
        get_directory_property(project DIRECTORY "${dir}" DEFINITION PROJECT_NAME)

        if (NOT project STREQUAL CMAKE_PROJECT_NAME)
            set(package "${project}")
        endif ()
    endif ()

    if (NOT package STREQUAL "")
        get_directory_property(targets DIRECTORY "${dir}" BUILDSYSTEM_TARGETS)

        foreach (target IN LISTS targets)
            get_target_property(folder ${target} FOLDER)

            if (NOT folder)
                set(folder "External/${package}")
            elseif (folder STREQUAL package OR folder MATCHES "^${package}/")
                set(folder "External/${folder}")
            else ()
                set(folder "External/${package}/${folder}")
            endif ()

            set_target_properties(${target} PROPERTIES FOLDER "${folder}")
        endforeach ()
    endif ()

    get_directory_property(children DIRECTORY "${dir}" SUBDIRECTORIES)

    foreach (child IN LISTS children)
        _makeasound_group_directory("${child}" "${package}")
    endforeach ()
endfunction()
