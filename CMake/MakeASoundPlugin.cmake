# makeasound_add_plugin(<Name>
#         FORMATS <format>...
#         SOURCES <file>...
#         [OUTPUT_NAME <display name>]
#         [BUNDLE_ID <reverse.dns.id>]
#         [COMPANY <vendor>]
#         [VERSION <x.y.z>]
#         [FOLDER <ide folder>])
#
# Builds the plugin's sources once as the static core <Name>, linked into one
# target per format: <Name>-Standalone, an app, and <Name>-VST3, a module built
# into the bundle <build>/VST3/<OUTPUT_NAME>.vst3. The company and display name go
# into eacp's embedded app info; the app's settings are filed under the module's
# vendor and the plugin's name. VERSION (default 1.0.0) is the bundles' version
# string; a VST3 host reads the version from the factory instead. Every target
# goes in the IDE folder <Name>, nested under CMAKE_FOLDER when the caller set
# one, unless FOLDER names another. A format whose library was not built is
# skipped with a status line; targets are global, so this works from a CPM
# consumer's directory too. With MAKEASOUND_INSTALL_PLUGINS on, each plug-in
# bundle is also copied into the user's plug-in folder after every build.

function(makeasound_add_plugin name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG ""
            "OUTPUT_NAME;BUNDLE_ID;COMPANY;VERSION;FOLDER" "FORMATS;SOURCES")

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

    if (NOT ARG_VERSION)
        set(ARG_VERSION "1.0.0")
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
                        MACOSX_BUNDLE_BUNDLE_VERSION "${ARG_VERSION}"
                        MACOSX_BUNDLE_SHORT_VERSION_STRING "${ARG_VERSION}"
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
        elseif (format STREQUAL "VST3")
            if (NOT TARGET MakeASoundVST3)
                message(STATUS "${name}: VST3 format skipped (MakeASoundVST3 not built)")
                continue()
            endif ()

            _makeasound_add_vst3(${name})
        else ()
            message(FATAL_ERROR
                    "makeasound_add_plugin(${name}): unknown format '${format}'")
        endif ()
    endforeach ()
endfunction()

# <Name>-VST3: the module, the SDK's platform entry and our GetPluginFactory, laid
# out as the VST3 bundle every host scans for. Reads the caller's ARG_* variables.
function(_makeasound_add_vst3 name)
    get_target_property(vst3_entry MakeASoundVST3 MAKEASOUND_VST3_ENTRY)
    get_target_property(vst3_main vst3sdk MAKEASOUND_VST3_SDK_MAIN)

    set(target ${name}-VST3)
    add_library(${target} MODULE "${vst3_entry}" "${vst3_main}")
    # The format first, as for the Standalone: the core defines describeModule().
    target_link_libraries(${target} PRIVATE MakeASoundVST3 ${name})

    # Every bundle in one folder, to hand PluginValidator as a whole.
    set(bundle_dir "${CMAKE_BINARY_DIR}/VST3")
    set(bundle "${bundle_dir}/${ARG_OUTPUT_NAME}.vst3")

    # $<1:...> keeps a multi-config generator from appending a per-config folder;
    # the import library, .exp and .pdb stay out of the bundle.
    set_target_properties(${target} PROPERTIES
            FOLDER "${ARG_FOLDER}"
            OUTPUT_NAME "${ARG_OUTPUT_NAME}"
            PREFIX ""
            ARCHIVE_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}>"
            PDB_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}>"
            MAKEASOUND_VST3_BUNDLE "${bundle}")

    if (APPLE)
        get_target_property(plist MakeASoundVST3 MAKEASOUND_VST3_PLIST)
        get_target_property(pkginfo MakeASoundVST3 MAKEASOUND_VST3_PKGINFO)
        get_target_property(exports vst3sdk MAKEASOUND_VST3_SDK_EXPORTS)

        # One identifier per format: the macOS Installer resolves a package's
        # components by bundle id, so the VST3 and the AU must not share one.
        set_target_properties(${target} PROPERTIES
                BUNDLE TRUE
                BUNDLE_EXTENSION vst3
                LIBRARY_OUTPUT_DIRECTORY "$<1:${bundle_dir}>"
                MACOSX_BUNDLE_INFO_PLIST "${plist}"
                MACOSX_BUNDLE_BUNDLE_NAME "${ARG_OUTPUT_NAME}"
                MACOSX_BUNDLE_GUI_IDENTIFIER "${ARG_BUNDLE_ID}.vst3"
                MACOSX_BUNDLE_BUNDLE_VERSION "${ARG_VERSION}"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "${ARG_VERSION}"
                MACOSX_BUNDLE_COPYRIGHT "${ARG_COMPANY}")

        # The SDK's list is exactly GetPluginFactory, bundleEntry and bundleExit,
        # so nothing else can coalesce with a host's or a sibling plugin's copy.
        target_link_options(${target} PRIVATE "LINKER:-exported_symbols_list,${exports}")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${exports}")

        # PkgInfo first, so the signature covers it.
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy "${pkginfo}"
                        "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/PkgInfo"
                COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>"
                VERBATIM)

        set(install_dir "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
    elseif (WIN32)
        set(arch_id "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")

        if (NOT arch_id)
            set(arch_id "${CMAKE_SYSTEM_PROCESSOR}")
        endif ()

        string(TOUPPER "${arch_id}" arch_id)

        if (arch_id MATCHES "^(X64|AMD64|X86_64)$")
            set(arch x86_64)
        elseif (arch_id MATCHES "^(ARM64|AARCH64)$")
            set(arch arm64)
        elseif (arch_id MATCHES "^(X86|I[3-6]86)$")
            set(arch x86)
        else ()
            message(FATAL_ERROR "${target}: unknown Windows architecture '${arch_id}'")
        endif ()

        # Only SMTG_EXPORT_SYMBOL functions leave a DLL, so there is no export list.
        set_target_properties(${target} PROPERTIES
                SUFFIX .vst3
                LIBRARY_OUTPUT_DIRECTORY "$<1:${bundle}/Contents/${arch}-win>")

        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Programs/Common/VST3" install_dir)
    else ()
        get_target_property(exports MakeASoundVST3 MAKEASOUND_VST3_LINUX_EXPORTS)

        set_target_properties(${target} PROPERTIES
                SUFFIX .so
                LIBRARY_OUTPUT_DIRECTORY
                "$<1:${bundle}/Contents/${CMAKE_SYSTEM_PROCESSOR}-linux>")

        # local: * binds the module's own MakeASound, eacp and Miro internally.
        target_link_options(${target} PRIVATE "LINKER:--version-script=${exports}")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${exports}")

        set(install_dir "$ENV{HOME}/.vst3")
    endif ()

    if (MAKEASOUND_INSTALL_PLUGINS)
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -D "SOURCE=${bundle}" -D "DEST_DIR=${install_dir}"
                        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/InstallPluginBundle.cmake"
                VERBATIM)
    endif ()
endfunction()
