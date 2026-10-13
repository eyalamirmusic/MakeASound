# makeasound_add_plugin(<Name>
#         FORMATS <format>...
#         SOURCES <file>...
#         [OUTPUT_NAME <display name>]
#         [BUNDLE_ID <reverse.dns.id>]
#         [COMPANY <vendor>]
#         [VERSION <x.y.z>]
#         [FOLDER <ide folder>]
#         [NO_AUDIO_INPUT]
#         [STANDALONE_SOURCES <file>... [STANDALONE_LIBRARIES <target>...]])
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
#
# On Apple the app's plist carries NSMicrophoneUsageDescription, which the
# standalone needs the moment a user picks an input, unless NO_AUDIO_INPUT says
# the plugin declares no input bus (an instrument), whose settings then offer no
# input and which never asks for the microphone. The layout is only known at run
# time, so the flag is the caller's to keep true. On iOS the plist also gains
# UIBackgroundModes audio, so the plugin keeps playing behind another app.
#
# STANDALONE_SOURCES replaces the default app's main with the plugin's own: the
# sources define main() and the app class, composing StandaloneHost and any of
# the pieces in <MakeASound/Plugin/Standalone/MakeASoundStandalone.h>, and
# STANDALONE_LIBRARIES is whatever else they link. The executable still links
# MakeASoundStandalone, so the pieces are there to take and the default app's
# objects, unreferenced, stay out of it; the bundle, plist, microphone string and
# signing are the same as the default app's.
#
# AUv3 is iOS's AU: <Name>-AUv3, an app extension inside the standalone app at
# <OUTPUT_NAME>.app/PlugIns/<OUTPUT_NAME>.appex, identifier <BUNDLE_ID>.auv3, so it
# needs Standalone in the same call (a custom app included) and is skipped with a
# status line off iOS. The appex is an executable of its own over NSExtensionMain
# linking the static core again. Its Info.plist comes from describeModule(), which
# an iOS binary cannot run on the build machine to read: the first plugin with
# AUv3 adds MakeASoundDescribeBuild, an ExternalProject that configures this same
# top-level project for macOS with MAKEASOUND_DESCRIBE_BUILD on, CPM pointed at the
# sources this tree already fetched, and builds <Name>-Describe for each, the core
# again from the same sources plus MakeASoundAUDescribe; the plugin's sources must
# therefore build for macOS too. MAKEASOUND_DESCRIBE_CMAKE_ARGS adds arguments to
# that configure. Under Ninja the bundles are laid out by hand and signed ad hoc,
# inside out, for the simulator and left unsigned for a device; under Xcode the app
# embeds the extension and Xcode signs both: automatically for MAKEASOUND_IOS_TEAM,
# otherwise ad hoc for the simulator and not at all for a device.

# The Apple development team an iOS tree signs with under the Xcode generator.
set(MAKEASOUND_IOS_TEAM "" CACHE STRING
        "Apple development team id for signing iOS apps and their AUv3 under Xcode")
set(MAKEASOUND_DESCRIBE_CMAKE_ARGS "" CACHE STRING
        "Extra configure arguments for an iOS tree's host build of the AUv3 plist tools")

function(makeasound_add_plugin name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "NO_AUDIO_INPUT"
            "OUTPUT_NAME;BUNDLE_ID;COMPANY;VERSION;FOLDER"
            "FORMATS;SOURCES;STANDALONE_SOURCES;STANDALONE_LIBRARIES")

    if (NOT ARG_SOURCES)
        message(FATAL_ERROR "makeasound_add_plugin(${name}): no SOURCES")
    endif ()

    if (NOT ARG_FORMATS)
        message(FATAL_ERROR "makeasound_add_plugin(${name}): no FORMATS")
    endif ()

    if ((ARG_STANDALONE_SOURCES OR ARG_STANDALONE_LIBRARIES)
            AND NOT "Standalone" IN_LIST ARG_FORMATS)
        message(FATAL_ERROR "makeasound_add_plugin(${name}): STANDALONE_SOURCES "
                "or STANDALONE_LIBRARIES without the Standalone format")
    endif ()

    if ("AUv3" IN_LIST ARG_FORMATS AND NOT "Standalone" IN_LIST ARG_FORMATS)
        message(FATAL_ERROR "makeasound_add_plugin(${name}): AUv3 without the "
                "Standalone format, the app that carries it")
    endif ()

    if (ARG_STANDALONE_LIBRARIES AND NOT ARG_STANDALONE_SOURCES)
        message(FATAL_ERROR "makeasound_add_plugin(${name}): STANDALONE_LIBRARIES "
                "without STANDALONE_SOURCES")
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

    # An iOS tree's host sub-build: the core and, for an AUv3, its plist tool.
    if (MAKEASOUND_DESCRIBE_BUILD)
        if ("AUv3" IN_LIST ARG_FORMATS)
            _makeasound_add_describe(${name})
        endif ()

        return()
    endif ()

    foreach (format IN LISTS ARG_FORMATS)
        if (format STREQUAL "Standalone")
            if (NOT TARGET MakeASoundStandalone)
                message(STATUS "${name}: Standalone format skipped "
                        "(MakeASoundStandalone not built)")
                continue()
            endif ()

            if (ARG_STANDALONE_SOURCES)
                set(standalone_sources ${ARG_STANDALONE_SOURCES})
            else ()
                get_target_property(standalone_sources MakeASoundStandalone
                        MAKEASOUND_STANDALONE_MAIN)
            endif ()

            set(target ${name}-Standalone)
            add_executable(${target} ${standalone_sources})
            # The format before the core: the core defines describeModule(), which the
            # format references, and a single-pass linker resolves left to right.
            target_link_libraries(${target} PRIVATE
                    MakeASoundStandalone ${ARG_STANDALONE_LIBRARIES} ${name})

            # Set before eacp_set_gui_subsystem, which reads both into AppInfo.
            set_target_properties(${target} PROPERTIES
                    FOLDER "${ARG_FOLDER}"
                    OUTPUT_NAME "${ARG_OUTPUT_NAME}"
                    MACOSX_BUNDLE_BUNDLE_NAME "${ARG_OUTPUT_NAME}"
                    EACP_COMPANY_NAME "${ARG_COMPANY}"
                    EACP_APP_VERSION "${ARG_VERSION}")

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

            if (APPLE AND NOT ARG_NO_AUDIO_INPUT)
                eacp_add_plist_entries(${target} NSMicrophoneUsageDescription
                        "${ARG_OUTPUT_NAME} processes audio from the input you pick.")
            endif ()

            # An array even with one item, which eacp_add_plist_entries cannot write.
            if (IOS)
                eacp_plist_array(modes audio)
                eacp_append_plist_xml(${target}
                        "\t<key>UIBackgroundModes</key>\n\t${modes}\n")
            endif ()

            if (IOS)
                _makeasound_ios_bundle(${target} APPL)
            elseif (APPLE)
                add_custom_command(TARGET ${target} POST_BUILD
                        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>"
                        VERBATIM)
            endif ()
        elseif (format STREQUAL "AUv3")
            # After the loop, which may not have made the app yet.
            continue()
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

    if ("AUv3" IN_LIST ARG_FORMATS)
        if (NOT IOS)
            message(STATUS "${name}: AUv3 format skipped (iOS only; macOS ships AU)")
        elseif (NOT TARGET MakeASoundAUv3)
            message(STATUS "${name}: AUv3 format skipped (MakeASoundAUv3 not built)")
        elseif (NOT TARGET ${name}-Standalone)
            message(STATUS "${name}: AUv3 format skipped (no standalone app to carry it)")
        else ()
            _makeasound_add_auv3(${name})
        endif ()
    endif ()
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

# The simulator or a device, from the sysroot the iOS tree was configured with:
# iPhoneSimulator or iPhoneOS, the platform names a bundle's plist carries.
function(_makeasound_ios_platform out_var)
    if (CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
        set(${out_var} iPhoneSimulator PARENT_SCOPE)
    else ()
        set(${out_var} iPhoneOS PARENT_SCOPE)
    endif ()
endfunction()

# An iOS bundle's packaging, the app's (APPL) or the extension's (XPC!), for both
# generators. Xcode writes the platform keys itself and signs: automatically with
# MAKEASOUND_IOS_TEAM, otherwise ad hoc, and a device build without a team not at
# all. Under Ninja the plist states the platform, the minimum OS and both device
# families, which a bundle laid out by hand has nobody else to write, and the
# bundle is signed ad hoc after the build for the simulator only: a device needs a
# real identity, which is Xcode's job, so a device build stays unsigned. An
# extension's keys are added to its generated plist by _makeasound_add_auv3.
function(_makeasound_ios_bundle target type)
    if (CMAKE_GENERATOR STREQUAL "Xcode")
        set_target_properties(${target} PROPERTIES
                XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")

        if (MAKEASOUND_IOS_TEAM)
            set_target_properties(${target} PROPERTIES
                    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${MAKEASOUND_IOS_TEAM}"
                    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
                    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development")
        else ()
            set_target_properties(${target} PROPERTIES
                    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Manual
                    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
                    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
                    "XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED[sdk=iphoneos*]" NO)
        endif ()

        return()
    endif ()

    if (type STREQUAL "APPL")
        _makeasound_ios_platform(platform)
        set(xml "\t<key>CFBundleSupportedPlatforms</key>\n"
                "\t<array><string>${platform}</string></array>\n"
                "\t<key>UIDeviceFamily</key>\n"
                "\t<array><integer>1</integer><integer>2</integer></array>\n")

        if (CMAKE_OSX_DEPLOYMENT_TARGET)
            list(APPEND xml "\t<key>MinimumOSVersion</key>\n"
                    "\t<string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>\n")
        endif ()

        string(JOIN "" xml ${xml})
        eacp_append_plist_xml(${target} "${xml}")
    endif ()

    if (CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND codesign --force --sign - --timestamp=none
                        "$<TARGET_BUNDLE_DIR:${target}>"
                VERBATIM)
    endif ()
endfunction()

# <Name>-AUv3: the app extension, its executable a stub over NSExtensionMain beside
# the module's AudioUnit.mm and view controller, compiled with this bundle's class
# suffix, and the core linked again. Its plist is written by <Name>-Describe from
# the host sub-build before it links. Reads the caller's ARG_* variables.
function(_makeasound_add_auv3 name)
    get_target_property(auv3_main MakeASoundAUv3 MAKEASOUND_AUV3_MAIN)
    get_target_property(auv3_sources MakeASoundAUv3 MAKEASOUND_AUV3_SOURCES)
    get_target_property(auv3_view_libraries MakeASoundAUv3 MAKEASOUND_AUV3_VIEW_LIBRARIES)
    get_target_property(auv3_has_view MakeASoundAUv3 MAKEASOUND_AUV3_HAS_VIEW)
    get_target_property(auv3_principal MakeASoundAUv3 MAKEASOUND_AUV3_PRINCIPAL_CLASS)

    set(app ${name}-Standalone)
    set(target ${name}-AUv3)

    add_executable(${target} "${auv3_main}" ${auv3_sources})
    # The format first, as for the others: the core defines describeModule().
    target_link_libraries(${target} PRIVATE
            MakeASoundAUv3 ${name} ${auv3_view_libraries})

    # The Objective-C runtime is one per process, and a host may load two of our
    # extensions into one, so every class is named for this bundle, as the AU
    # view's are.
    string(MAKE_C_IDENTIFIER "${ARG_BUNDLE_ID}_${ARG_VERSION}" suffix)
    target_compile_definitions(${target} PRIVATE MAKEASOUND_AUV3_CLASS_SUFFIX=${suffix})

    # An app extension may call only extension-safe API, checked at compile and
    # link time.
    target_compile_options(${target} PRIVATE -fapplication-extension
            $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_options(${target} PRIVATE -fapplication-extension)

    set_target_properties(${target} PROPERTIES
            FOLDER "${ARG_FOLDER}"
            OUTPUT_NAME "${ARG_OUTPUT_NAME}"
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

    if (auv3_has_view)
        set(has_view 1)
    else ()
        set(has_view 0)
    endif ()

    _makeasound_describe_tool(${name} tool)
    set(plist "${CMAKE_CURRENT_BINARY_DIR}/${target}-Info.plist")
    # An extension's version must be its app's, so VERSION replaces the module's.
    set(commands COMMAND "${tool}" "${ARG_OUTPUT_NAME}" "${ARG_BUNDLE_ID}.auv3"
            "${ARG_OUTPUT_NAME}" "${auv3_principal}_${suffix}" ${has_view} "${plist}"
            COMMAND plutil -replace CFBundleVersion -string "${ARG_VERSION}" "${plist}"
            COMMAND plutil -replace CFBundleShortVersionString -string "${ARG_VERSION}"
                    "${plist}")

    # The platform keys writeAppExtensionPlist leaves to the packaging; Xcode
    # writes its own and must not find them in the template.
    if (NOT CMAKE_GENERATOR STREQUAL "Xcode")
        _makeasound_ios_platform(platform)
        list(APPEND commands
                COMMAND plutil -insert CFBundleSupportedPlatforms
                        -json "[\"${platform}\"]" "${plist}"
                COMMAND plutil -insert UIDeviceFamily -json "[1,2]" "${plist}")

        if (CMAKE_OSX_DEPLOYMENT_TARGET)
            list(APPEND commands COMMAND plutil -insert MinimumOSVersion
                    -string "${CMAKE_OSX_DEPLOYMENT_TARGET}" "${plist}")
        endif ()
    endif ()

    add_custom_command(OUTPUT "${plist}"
            ${commands}
            DEPENDS "${tool}" MakeASoundDescribeBuild
            COMMENT "Writing the Info.plist of ${ARG_OUTPUT_NAME}.appex"
            VERBATIM)
    add_custom_target(${target}-Plist DEPENDS "${plist}")
    set_target_properties(${target}-Plist PROPERTIES FOLDER "${ARG_FOLDER}")
    add_dependencies(${target} ${target}-Plist)

    # The extension before the app, under either generator: Xcode's embedding
    # copies the product but does not build it.
    add_dependencies(${app} ${target})

    if (CMAKE_GENERATOR STREQUAL "Xcode")
        # The plist is Xcode's to process (it adds the platform keys); the app
        # embeds the extension, and Xcode signs the extension before the app.
        set_target_properties(${target} PROPERTIES
                MACOSX_BUNDLE TRUE
                BUNDLE_EXTENSION appex
                XCODE_PRODUCT_TYPE com.apple.product-type.app-extension
                XCODE_ATTRIBUTE_INFOPLIST_FILE "${plist}"
                XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${ARG_BUNDLE_ID}.auv3"
                XCODE_ATTRIBUTE_APPLICATION_EXTENSION_API_ONLY YES
                # Xcode drops the per-language -fobjc-arc above.
                XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
                XCODE_ATTRIBUTE_SKIP_INSTALL YES)
        _makeasound_ios_bundle(${target} XPC!)
        set_target_properties(${app} PROPERTIES XCODE_EMBED_APP_EXTENSIONS ${target})
        return()
    endif ()

    # Laid out by hand inside the app's bundle, as the .component is: CMake would
    # wrap a MACOSX_BUNDLE executable in an .app of its own. $<1:...> keeps a
    # multi-config generator from appending a per-config folder.
    get_target_property(app_dir ${app} RUNTIME_OUTPUT_DIRECTORY)

    if (NOT app_dir)
        set(app_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif ()

    set(app_bundle "${app_dir}/${ARG_OUTPUT_NAME}.app")
    set(appex "${app_bundle}/PlugIns/${ARG_OUTPUT_NAME}.appex")

    set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE FALSE
            RUNTIME_OUTPUT_DIRECTORY "$<1:${appex}>"
            MAKEASOUND_AUV3_BUNDLE "${appex}")

    # A new plist relinks the extension, so the copy and the signature follow it.
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${plist}")

    # The app signs itself after it links, over the extension inside it; the
    # extension, built first, signs itself and then re-signs an app already built,
    # whose seal names the extension's signature.
    set(post COMMAND ${CMAKE_COMMAND} -E copy "${plist}" "${appex}/Info.plist")

    if (CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
        list(APPEND post
                COMMAND codesign --force --sign - --timestamp=none "${appex}"
                COMMAND ${CMAKE_COMMAND} -D "BUNDLE=${app_bundle}"
                        -D "EXECUTABLE=${ARG_OUTPUT_NAME}"
                        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/SignBuiltBundle.cmake")
    endif ()

    add_custom_command(TARGET ${target} POST_BUILD ${post} VERBATIM)
endfunction()

# The path of <Name>-Describe in the host sub-build, which the first call schedules
# for the end of the top-level configure, when every plugin and every CPM package
# is known.
function(_makeasound_describe_tool name out_var)
    set_property(GLOBAL APPEND PROPERTY MAKEASOUND_DESCRIBE_PLUGINS ${name})
    get_property(scheduled GLOBAL PROPERTY MAKEASOUND_DESCRIBE_SCHEDULED)

    if (NOT scheduled)
        set_property(GLOBAL PROPERTY MAKEASOUND_DESCRIBE_SCHEDULED TRUE)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
                CALL _makeasound_add_describe_build)
    endif ()

    set(${out_var} "${CMAKE_BINARY_DIR}/MakeASoundDescribe/Describe/${name}-Describe"
            PARENT_SCOPE)
endfunction()

# MakeASoundDescribeBuild: this top-level project again, configured for the build
# machine with MAKEASOUND_DESCRIBE_BUILD on and every CPM package pointed at the
# source this tree already has, building each <Name>-Describe. Its steps run with
# the iOS SDK's environment cleared, since an Xcode build exports SDKROOT and the
# deployment target to every script, and clang and xcrun both read them.
function(_makeasound_add_describe_build)
    include(ExternalProject)

    get_property(plugins GLOBAL PROPERTY MAKEASOUND_DESCRIBE_PLUGINS)
    set(binary "${CMAKE_BINARY_DIR}/MakeASoundDescribe")
    set(targets "")
    set(tools "")

    foreach (plugin IN LISTS plugins)
        list(APPEND targets ${plugin}-Describe)
        list(APPEND tools "${binary}/Describe/${plugin}-Describe")
    endforeach ()

    if (CMAKE_GENERATOR MATCHES "Ninja")
        set(generator Ninja)
        set(make_program "${CMAKE_MAKE_PROGRAM}")
    else ()
        find_program(MAKEASOUND_NINJA ninja)

        if (MAKEASOUND_NINJA)
            set(generator Ninja)
            set(make_program "${MAKEASOUND_NINJA}")
        else ()
            set(generator "Unix Makefiles")
            set(make_program "")
        endif ()
    endif ()

    set(args -G "${generator}"
            -DCMAKE_BUILD_TYPE=Debug
            -DCMAKE_OSX_SYSROOT=macosx
            -DMAKEASOUND_DESCRIBE_BUILD=ON
            -DMAKEASOUND_BUILD_PLUGIN=ON
            -DMAKEASOUND_BUILD_APPS=OFF
            -DMAKEASOUND_BUILD_TESTS=OFF
            -DMAKEASOUND_INSTALL_PLUGINS=OFF)

    if (make_program)
        list(APPEND args "-DCMAKE_MAKE_PROGRAM=${make_program}")
    endif ()

    foreach (option MAKEASOUND_BUILD_EXAMPLES MAKEASOUND_UNITY_BUILD MAKEASOUND_CI_BUILD)
        if (DEFINED ${option})
            list(APPEND args "-D${option}=${${option}}")
        endif ()
    endforeach ()

    if (CPM_SOURCE_CACHE)
        list(APPEND args "-DCPM_SOURCE_CACHE=${CPM_SOURCE_CACHE}")
    endif ()

    foreach (package IN LISTS CPM_PACKAGES)
        set(source "${CPM_PACKAGE_${package}_SOURCE_DIR}")

        if (source AND EXISTS "${source}")
            list(APPEND args "-DCPM_${package}_SOURCE=${source}")
        endif ()
    endforeach ()

    list(APPEND args ${MAKEASOUND_DESCRIBE_CMAKE_ARGS})

    set(env ${CMAKE_COMMAND} -E env --unset=SDKROOT --unset=IPHONEOS_DEPLOYMENT_TARGET
            --unset=MACOSX_DEPLOYMENT_TARGET)

    ExternalProject_Add(MakeASoundDescribeBuild
            SOURCE_DIR "${CMAKE_SOURCE_DIR}"
            BINARY_DIR "${binary}"
            PREFIX "${binary}-prefix"
            DOWNLOAD_COMMAND ""
            UPDATE_COMMAND ""
            CONFIGURE_COMMAND ${env} ${CMAKE_COMMAND} -S <SOURCE_DIR> -B <BINARY_DIR>
                    ${args}
            BUILD_COMMAND ${env} ${CMAKE_COMMAND} --build <BINARY_DIR> --target ${targets}
            BUILD_ALWAYS TRUE
            BUILD_BYPRODUCTS ${tools}
            INSTALL_COMMAND ""
            TEST_COMMAND ""
            USES_TERMINAL_CONFIGURE TRUE
            USES_TERMINAL_BUILD TRUE)

    set_target_properties(MakeASoundDescribeBuild PROPERTIES FOLDER Tools)
endfunction()

# <Name>-Describe, in the host sub-build: MakeASoundAUDescribe's DescribeMain.cpp
# and the plugin's core, written into one folder the iOS tree knows.
function(_makeasound_add_describe name)
    get_target_property(describe_main MakeASoundAUDescribe MAKEASOUND_AU_DESCRIBE_MAIN)

    set(target ${name}-Describe)
    add_executable(${target} "${describe_main}")
    target_link_libraries(${target} PRIVATE MakeASoundAUDescribe ${name})
    set_target_properties(${target} PROPERTIES
            FOLDER "${ARG_FOLDER}"
            MACOSX_BUNDLE FALSE
            RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/Describe>")
endfunction()
