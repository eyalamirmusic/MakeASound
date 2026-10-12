# makeasound_add_web_editor(<Name>
#         WEB_DIR <dir>
#         SOURCES <file>...
#         FALLBACK_SOURCES <file>...
#         [API <type>... API_HEADER <header>...]
#         [CATEGORY <category>])
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

function(makeasound_add_web_editor name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG ""
            "WEB_DIR;CATEGORY" "SOURCES;FALLBACK_SOURCES;API;API_HEADER")

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

    set(vite_depends "")

    # The codegen tool is absent when cross-compiling; the committed TypeScript
    # in src/generated stands in for its output then.
    if (TARGET ${schema}_Codegen)
        target_link_libraries(${schema}_Codegen PRIVATE
                eacp-core eacp-webview-codegen MakeASoundPluginWeb)
        set_target_properties(${schema}_Codegen PROPERTIES
                FOLDER "${folder}"
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
        set(vite_depends DEPENDS ${schema}_Codegen)
    endif ()

    eacp_webview_add_vite(${name}
            SOURCE_DIR "${ARG_WEB_DIR}"
            NAMESPACE ${name}Editor
            CATEGORY ${ARG_CATEGORY}
            ${vite_depends})

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
