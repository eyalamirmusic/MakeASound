# cmake -D PLUGIN_CMAKE=<CMake/MakeASoundPlugin.cmake> -D CASE=<case> -P Install.cmake
#
# makeasound_add_plugin's copy of each bundle into the user's plug-in folder, with
# no target made: add_custom_command and set_target_properties, which script mode
# lacks, are replaced by stubs, and the first records every command it is given.
# install_bundles is set by the caller's function scope, as makeasound_add_plugin
# sets it for the format helpers it calls.
#
#   helper      _makeasound_install_bundle, which every format installs through:
#               nothing with install_bundles off or unset, otherwise one POST_BUILD
#               command running InstallPluginBundle.cmake on the bundle
#   aaxWindows  the Windows AAX, which once installed whenever
#               MAKEASOUND_INSTALL_PLUGINS was on, NO_INSTALL or not: nothing with
#               install_bundles off, and otherwise the bundle copied into Avid's
#               folder under CommonProgramFiles, or its default when that is unset
include("${PLUGIN_CMAKE}")

get_filename_component(plugin_cmake_dir "${PLUGIN_CMAKE}" DIRECTORY)
set(install_script "${plugin_cmake_dir}/InstallPluginBundle.cmake")

function(add_custom_command)
    string(JOIN "|" call ${ARGV})
    set_property(GLOBAL APPEND PROPERTY RECORDED_COMMANDS "${call}")
endfunction()

function(set_target_properties)
endfunction()

macro(clear_recorded)
    set_property(GLOBAL PROPERTY RECORDED_COMMANDS "")
endmacro()

function(expect_recorded)
    get_property(recorded GLOBAL PROPERTY RECORDED_COMMANDS)

    if (NOT "${recorded}" STREQUAL "${ARGN}")
        message(SEND_ERROR "recorded '${recorded}', expected '${ARGN}'")
    endif ()
endfunction()

# The command _makeasound_install_bundle adds for <target>, as the stub records it:
# through a list, since the stub's ARGV splits an argument at any ';' too.
function(install_command out_var target bundle dest_dir)
    set(parts TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND}
            -D "SOURCE=${bundle}" -D "DEST_DIR=${dest_dir}" -P "${install_script}"
            VERBATIM)
    string(JOIN "|" call ${parts})
    set(${out_var} "${call}" PARENT_SCOPE)
endfunction()

# What makeasound_add_plugin's scope holds when it calls a format helper, with the
# helper called from inside it.
function(as_plugin install command)
    set(install_bundles ${install})
    cmake_language(CALL ${command} ${ARGN})
endfunction()

function(as_plugin_without_setting command)
    unset(install_bundles)
    cmake_language(CALL ${command} ${ARGN})
endfunction()

set(bundle "/build/VST3/My Plugin.vst3")
set(dest "/Users/me/Library/Audio/Plug-Ins/VST3")

if (CASE STREQUAL "helper")
    clear_recorded()
    as_plugin(OFF _makeasound_install_bundle MyPlugin-VST3 "${bundle}" "${dest}")
    expect_recorded()

    clear_recorded()
    as_plugin_without_setting(_makeasound_install_bundle MyPlugin-VST3 "${bundle}"
            "${dest}")
    expect_recorded()

    clear_recorded()
    as_plugin(ON _makeasound_install_bundle MyPlugin-VST3 "${bundle}" "${dest}")
    install_command(expected MyPlugin-VST3 "${bundle}" "${dest}")
    expect_recorded("${expected}")
elseif (CASE STREQUAL "aaxWindows")
    set(bundle "C:/build/AAX/My Plugin.aaxplugin")

    # NO_INSTALL with MAKEASOUND_INSTALL_PLUGINS on: install_bundles is off.
    set(MAKEASOUND_INSTALL_PLUGINS ON)
    set(ENV{CommonProgramFiles} "/Shared/Common Files")
    clear_recorded()
    as_plugin(OFF _makeasound_add_aax_windows MyPlugin MyPlugin-AAX "${bundle}")
    expect_recorded()

    # No drive letter above: off Windows, file(TO_CMAKE_PATH) splits a path list
    # at ':', so the default's expectation goes through it too.
    clear_recorded()
    as_plugin(ON _makeasound_add_aax_windows MyPlugin MyPlugin-AAX "${bundle}")
    install_command(expected MyPlugin-AAX "${bundle}"
            "/Shared/Common Files/Avid/Audio/Plug-Ins")
    expect_recorded("${expected}")

    unset(ENV{CommonProgramFiles})
    clear_recorded()
    as_plugin(ON _makeasound_add_aax_windows MyPlugin MyPlugin-AAX "${bundle}")
    file(TO_CMAKE_PATH "C:/Program Files/Common Files/Avid/Audio/Plug-Ins" default_dir)
    install_command(expected MyPlugin-AAX "${bundle}" "${default_dir}")
    expect_recorded("${expected}")
else ()
    message(FATAL_ERROR "unknown CASE '${CASE}'")
endif ()
