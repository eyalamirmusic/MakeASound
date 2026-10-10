# makeasound_add_plugin(<Name>
#         FORMATS <format>...
#         SOURCES <file>...
#         [OUTPUT_NAME <display name>]
#         [BUNDLE_ID <reverse.dns.id>]
#         [COMPANY <vendor>]
#         [FOLDER <ide folder>])
#
# Builds the plugin's sources once as the static core <Name>, linked into one
# target per format: <Name>-Standalone today. The company and display name go
# into eacp's embedded app info; the app's settings are filed under the module's
# vendor and the plugin's name. Every target goes in the IDE folder <Name>,
# nested under CMAKE_FOLDER when the caller set one, unless FOLDER names another.
# A format whose library was not built is skipped with a status line; targets are
# global, so this works from a CPM consumer's directory too.

function(makeasound_add_plugin name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG ""
            "OUTPUT_NAME;BUNDLE_ID;COMPANY;FOLDER" "FORMATS;SOURCES")

    if (NOT ARG_SOURCES)
        message(FATAL_ERROR "makeasound_add_plugin(${name}): no SOURCES")
    endif ()

    if (NOT ARG_FORMATS)
        message(FATAL_ERROR "makeasound_add_plugin(${name}): no FORMATS")
    endif ()

    if (NOT ARG_OUTPUT_NAME)
        set(ARG_OUTPUT_NAME "${name}")
    endif ()

    if (NOT ARG_BUNDLE_ID)
        string(TOLOWER "com.makeasound.${name}" ARG_BUNDLE_ID)
    endif ()

    if (NOT ARG_COMPANY)
        set(ARG_COMPANY "MakeASound")
    endif ()

    if (NOT ARG_FOLDER)
        if (CMAKE_FOLDER)
            set(ARG_FOLDER "${CMAKE_FOLDER}/${name}")
        else ()
            set(ARG_FOLDER "${name}")
        endif ()
    endif ()

    add_library(${name} STATIC ${ARG_SOURCES})
    target_link_libraries(${name} PUBLIC MakeASoundPlugin)
    set_target_properties(${name} PROPERTIES FOLDER "${ARG_FOLDER}")

    if (COMMAND set_makeasound_warnings)
        set_makeasound_warnings(${name})
    endif ()

    foreach (format IN LISTS ARG_FORMATS)
        if (format STREQUAL "Standalone")
            if (NOT TARGET MakeASoundStandalone)
                message(STATUS "${name}: Standalone format skipped "
                        "(MakeASoundStandalone not built)")
                continue()
            endif ()

            get_target_property(standalone_main MakeASoundStandalone
                    MAKEASOUND_STANDALONE_MAIN)

            set(target ${name}-Standalone)
            add_executable(${target} "${standalone_main}")
            # The format before the core: the core defines describeModule(), which the
            # format references, and a single-pass linker resolves left to right.
            target_link_libraries(${target} PRIVATE MakeASoundStandalone ${name})

            # Set before eacp_set_gui_subsystem, which reads both into AppInfo.
            set_target_properties(${target} PROPERTIES
                    FOLDER "${ARG_FOLDER}"
                    OUTPUT_NAME "${ARG_OUTPUT_NAME}"
                    MACOSX_BUNDLE_BUNDLE_NAME "${ARG_OUTPUT_NAME}"
                    EACP_COMPANY_NAME "${ARG_COMPANY}")

            if (APPLE)
                set_target_properties(${target} PROPERTIES
                        MACOSX_BUNDLE TRUE
                        MACOSX_BUNDLE_GUI_IDENTIFIER "${ARG_BUNDLE_ID}"
                        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${ARG_BUNDLE_ID}")
            endif ()

            eacp_set_gui_subsystem(${target})
            set_default_target_setting(${target})

            if (APPLE)
                eacp_add_plist_entries(${target} NSMicrophoneUsageDescription
                        "${ARG_OUTPUT_NAME} processes audio from the input you pick.")

                add_custom_command(TARGET ${target} POST_BUILD
                        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>"
                        VERBATIM)
            endif ()
        else ()
            message(FATAL_ERROR
                    "makeasound_add_plugin(${name}): unknown format '${format}'")
        endif ()
    endforeach ()
endfunction()
