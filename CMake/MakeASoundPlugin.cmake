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
# target per format: <Name>-Standalone, an app; <Name>-VST3, a module built into
# the bundle <build>/VST3/<OUTPUT_NAME>.vst3; and on macOS <Name>-AU, a module
# built into <build>/AU/<OUTPUT_NAME>.component, whose Info.plist <Name>-AUPlistGen
# writes from describeModule() after every link. The company and display name go
# into eacp's embedded app info; the app's settings are filed under the module's
# vendor and the plugin's name. VERSION (default 1.0.0) is the app's and the
# VST3 bundle's version string; a VST3 host reads the version from the factory
# instead, and the AU bundle takes ModuleDescription::version. Every target
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
    # Position independent because the VST3 module links it on Linux; a consumer's
    # own tree needs CMAKE_POSITION_INDEPENDENT_CODE on for whatever else it links.
    set_target_properties(${name} PROPERTIES
            FOLDER "${ARG_FOLDER}"
            POSITION_INDEPENDENT_CODE ON)

    if (MAKEASOUND_UNITY_BUILD)
        set_target_properties(${name} PROPERTIES UNITY_BUILD ON)
    endif ()

    if (COMMAND set_makeasound_target_settings)
        set_makeasound_target_settings(${name})
    endif ()

    # Release LTO whatever tree this runs in: eacp's Release archives are LTO
    # bitcode under Clang, and so is everything linked into a format target.
    set_target_properties(${name} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

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
        elseif (format STREQUAL "AU")
            if (NOT TARGET MakeASoundAU)
                message(STATUS "${name}: AU format skipped (MakeASoundAU not built)")
                continue()
            endif ()

            _makeasound_add_au(${name})
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
    set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

    # Every bundle in one folder, to hand PluginValidator as a whole.
    set(bundle_dir "${CMAKE_BINARY_DIR}/VST3")
    set(bundle "${bundle_dir}/${ARG_OUTPUT_NAME}.vst3")

    # $<1:...> keeps a multi-config generator from appending a per-config folder;
    # the import library, .exp and .pdb stay out of the bundle, and the .pdb is
    # named apart from the standalone's, which shares the output name and the
    # folder: two links writing one program database fail (LNK1201).
    set_target_properties(${target} PROPERTIES
            FOLDER "${ARG_FOLDER}"
            OUTPUT_NAME "${ARG_OUTPUT_NAME}"
            PREFIX ""
            ARCHIVE_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}>"
            PDB_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}>"
            PDB_NAME "${ARG_OUTPUT_NAME}-VST3"
            COMPILE_PDB_NAME "${ARG_OUTPUT_NAME}-VST3"
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

# <Name>-AU: the module, our MakeASoundAUFactory and MakeASoundAUWritePlist and the
# view classes named for this bundle, laid out as the .component bundle macOS
# scans. After the link MakeASoundAUPlistGen loads the module and has it write the
# Info.plist its AudioComponents come from. Reads the caller's ARG_* variables.
function(_makeasound_add_au name)
    get_target_property(au_entry MakeASoundAU MAKEASOUND_AU_ENTRY)
    get_target_property(au_exports MakeASoundAU MAKEASOUND_AU_EXPORTS)
    get_target_property(au_pkginfo MakeASoundAU MAKEASOUND_AU_PKGINFO)
    get_target_property(au_view_source MakeASoundAU MAKEASOUND_AU_VIEW_SOURCE)
    get_target_property(au_view_libraries MakeASoundAU MAKEASOUND_AU_VIEW_LIBRARIES)

    set(target ${name}-AU)
    add_library(${target} MODULE "${au_entry}" "${au_view_source}")
    # The format first, as for the others: the core defines describeModule().
    target_link_libraries(${target} PRIVATE MakeASoundAU ${name} ${au_view_libraries})

    # The Objective-C runtime is one per process, so two components defining one
    # class name would share whichever loaded first.
    string(MAKE_C_IDENTIFIER "${ARG_BUNDLE_ID}_${ARG_VERSION}" view_id)
    target_compile_definitions(${target} PRIVATE
            MAKEASOUND_AU_VIEW_CLASS=MakeASoundAUView_${view_id})

    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)

    set(bundle_dir "${CMAKE_BINARY_DIR}/AU")
    set(bundle "${bundle_dir}/${ARG_OUTPUT_NAME}.component")

    # Laid out by hand rather than as a BUNDLE target: CMake rewrites a bundle's
    # Info.plist on every configure, which would undo the generated one below
    # until the next relink. $<1:...> keeps a multi-config generator from
    # appending a per-config folder.
    set_target_properties(${target} PROPERTIES
            FOLDER "${ARG_FOLDER}"
            OUTPUT_NAME "${ARG_OUTPUT_NAME}"
            PREFIX ""
            SUFFIX ""
            # The SDK's headers include <expected>, in the view's Objective-C++ too.
            OBJCXX_STANDARD 23
            LIBRARY_OUTPUT_DIRECTORY "$<1:${bundle}/Contents/MacOS>"
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
            MAKEASOUND_AU_BUNDLE "${bundle}")

    # Only the factory and the plist writer leave the image, so nothing else can
    # coalesce with the copy another component in the same host process carries.
    target_link_options(${target} PRIVATE "LINKER:-exported_symbols_list,${au_exports}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${au_exports}")

    add_dependencies(${target} MakeASoundAUPlistGen)

    # The plist, then PkgInfo, then the signature over both. One identifier per
    # format: the macOS Installer resolves a package's components by bundle id.
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND $<TARGET_FILE:MakeASoundAUPlistGen> "$<TARGET_FILE:${target}>"
                    "${ARG_OUTPUT_NAME}" "${ARG_BUNDLE_ID}.component"
                    "${ARG_OUTPUT_NAME}" "${bundle}/Contents/Info.plist"
            COMMAND ${CMAKE_COMMAND} -E copy "${au_pkginfo}" "${bundle}/Contents/PkgInfo"
            COMMAND codesign --force --sign - "${bundle}"
            VERBATIM)

    if (MAKEASOUND_INSTALL_PLUGINS)
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -D "SOURCE=${bundle}"
                        -D "DEST_DIR=$ENV{HOME}/Library/Audio/Plug-Ins/Components"
                        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/InstallPluginBundle.cmake"
                VERBATIM)
    endif ()
endfunction()
