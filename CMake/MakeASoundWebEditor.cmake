# makeasound_add_web_editor(<Name>
#         WEB_DIR <dir>
#         SOURCES <file>...
#         FALLBACK_SOURCES <file>...
#         [API <type>... API_HEADER <header>...]
#         [CATEGORY <category>]
#         [SHARED_UI <dir>...]
#         [PRE_BUILD <target>...])
#
# Gives the static plugin core <Name> (from makeasound_add_plugin) a React page:
# the Miro schema of MakeASound::Web::ParameterApi and any further API, the
# generated TypeScript in <dir>/src/generated, the vite build of <dir>, and the
# built page embedded through ResEmbed under CATEGORY (default <Name>Editor).
# ResEmbed's register TU is an INTERFACE source of a static library, so every
# format target linking the core carries the page in its own image. SOURCES are
# added to the core; where MakeASoundPluginWeb is not a target (no native web
# runtime) FALLBACK_SOURCES are added instead and no npm runs. The core sees
# MAKEASOUND_WEB_DEV_SERVER, the vite dev server under EACP_WEBVIEW_DEV and ""
# otherwise, to pass as WebEditorOptions::devServer.
#
# SHARED_UI copies code several pages share into <dir>/src/shared before each
# vite build: the directory is emptied, then each SHARED_UI directory is copied
# in, in order, so a later one's file replaces an earlier one's of the same path.
# A shared file imports the page's bindings as '../generated/...'. Add src/shared
# to the page's .gitignore; it is rebuilt whenever a shared file changes.
#
# PRE_BUILD names targets of the project's own (a codegen step, say) that run
# before the vite build, after the schema codegen and the SHARED_UI copy, so they
# can read src/generated and src/shared. Whatever they write under <dir>/src is
# part of the page, and vite runs again when it changes. Both run with the core
# under EACP_WEBVIEW_DEV too, where the dev server serves what they wrote.

function(makeasound_add_web_editor name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG ""
            "WEB_DIR;CATEGORY"
            "SOURCES;FALLBACK_SOURCES;API;API_HEADER;SHARED_UI;PRE_BUILD")

    if (NOT TARGET ${name})
        message(FATAL_ERROR "makeasound_add_web_editor(${name}): no such target; "
                "call makeasound_add_plugin(${name} ...) first")
    endif ()

    if (NOT ARG_WEB_DIR)
        message(FATAL_ERROR "makeasound_add_web_editor(${name}): no WEB_DIR")
    endif ()

    if (ARG_API AND NOT ARG_API_HEADER)
        message(FATAL_ERROR "makeasound_add_web_editor(${name}): API needs API_HEADER")
    endif ()

    foreach (shared IN LISTS ARG_SHARED_UI)
        if (NOT IS_DIRECTORY "${shared}")
            message(FATAL_ERROR "makeasound_add_web_editor(${name}): SHARED_UI "
                    "'${shared}' is not a directory")
        endif ()
    endforeach ()

    foreach (step IN LISTS ARG_PRE_BUILD)
        if (NOT TARGET ${step})
            message(FATAL_ERROR "makeasound_add_web_editor(${name}): PRE_BUILD "
                    "'${step}' is not a target; define it first")
        endif ()
    endforeach ()

    if (NOT TARGET MakeASoundPluginWeb)
        message(STATUS "makeasound_add_web_editor(${name}): no web runtime, "
                "building the fallback editor")
        target_sources(${name} PRIVATE ${ARG_FALLBACK_SOURCES})
        return()
    endif ()

    if (NOT ARG_CATEGORY)
        set(ARG_CATEGORY "${name}Editor")
    endif ()

    set(schema ${name}EditorSchema)
    set(generated "${ARG_WEB_DIR}/src/generated")
    get_target_property(api_header MakeASoundPluginWeb MAKEASOUND_WEB_API_HEADER)
    get_target_property(folder ${name} FOLDER)

    miro_export(${schema}
            API MakeASound::Web::ParameterApi ${ARG_API}
            API_HEADER "${api_header}" ${ARG_API_HEADER}
            OUTPUT_DIR "${generated}"
            OUTPUT_NAME schema
            FORMATS ts backend ts-server bridge events hooks)

    eacp_webview_generate_backend(OUTPUT_DIR "${generated}" BASENAME schema)
    eacp_webview_generate_mock(OUTPUT_DIR "${generated}" BASENAME schema)
    eacp_webview_generate_react_hooks(OUTPUT_DIR "${generated}" BASENAME schema)
    eacp_target_uses_schema(${name} ${schema})
    set_target_properties(${schema} PROPERTIES FOLDER "${folder}")

    # What the vite build waits for, in the order it is listed: the schema
    # codegen, the shared copy, then the project's own steps.
    set(before_vite "")

    # The codegen tool is absent when cross-compiling; the committed TypeScript
    # in src/generated stands in for its output then.
    if (TARGET ${schema}_Codegen)
        target_link_libraries(${schema}_Codegen PRIVATE
                eacp-core eacp-webview-codegen MakeASoundPluginWeb)
        set_target_properties(${schema}_Codegen PROPERTIES
                FOLDER "${folder}"
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
        list(APPEND before_vite ${schema}_Codegen)
    endif ()

    # A file copy rather than a symlink or a vite alias, which would need the
    # page's config to know about it. The stamp is the vite build's file
    # dependency, so a shared edit rebuilds the page.
    set(shared_stamp "")

    if (ARG_SHARED_UI)
        set(shared_dir "${ARG_WEB_DIR}/src/shared")
        set(shared_stamp "${CMAKE_CURRENT_BINARY_DIR}/${name}-web-shared-ui.stamp")
        set(shared_sources "")
        set(copies "")

        foreach (shared IN LISTS ARG_SHARED_UI)
            file(GLOB_RECURSE files CONFIGURE_DEPENDS "${shared}/*")
            list(APPEND shared_sources ${files})
            list(APPEND copies
                    COMMAND ${CMAKE_COMMAND} -E copy_directory "${shared}" "${shared_dir}")
        endforeach ()

        add_custom_command(
                OUTPUT "${shared_stamp}"
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${shared_dir}"
                ${copies}
                COMMAND ${CMAKE_COMMAND} -E touch "${shared_stamp}"
                DEPENDS ${shared_sources}
                COMMENT "Copying the shared web UI into ${name}'s page"
                VERBATIM)

        add_custom_target(${name}WebSharedUI DEPENDS "${shared_stamp}")
        set_target_properties(${name}WebSharedUI PROPERTIES FOLDER "${folder}")

        if (TARGET ${schema}_Codegen)
            add_dependencies(${name}WebSharedUI ${schema}_Codegen)
        endif ()

        list(APPEND before_vite ${name}WebSharedUI)
    endif ()

    foreach (step IN LISTS ARG_PRE_BUILD)
        if (TARGET ${schema}_Codegen)
            add_dependencies(${step} ${schema}_Codegen)
        endif ()

        if (ARG_SHARED_UI)
            add_dependencies(${step} ${name}WebSharedUI)
        endif ()

        list(APPEND before_vite ${step})
    endforeach ()

    set(vite_depends "")

    if (before_vite OR shared_stamp)
        set(vite_depends DEPENDS ${before_vite} ${shared_stamp})
    endif ()

    eacp_webview_add_vite(${name}
            SOURCE_DIR "${ARG_WEB_DIR}"
            NAMESPACE ${name}Editor
            CATEGORY ${ARG_CATEGORY}
            ${vite_depends})

    # Under EACP_WEBVIEW_DEV there is no vite build to wait for them, and the
    # dev server still needs what they write.
    foreach (step IN LISTS before_vite)
        if (NOT step STREQUAL "${schema}_Codegen")
            add_dependencies(${name} ${step})
        endif ()
    endforeach ()

    if (EACP_WEBVIEW_DEV)
        set(dev_server "http://localhost:5173")
    else ()
        set(dev_server "")
    endif ()

    target_link_libraries(${name} PUBLIC MakeASoundPluginWeb)
    target_sources(${name} PRIVATE ${ARG_SOURCES})
    target_compile_definitions(${name} PRIVATE
            "MAKEASOUND_WEB_DEV_SERVER=\"${dev_server}\"")
endfunction()
