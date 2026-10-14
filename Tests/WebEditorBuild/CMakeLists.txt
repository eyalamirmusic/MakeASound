# makeasound_add_web_editor's build hooks, checked on two fixture pages whose
# "bundler" is build.mjs, a node script with no dependencies that writes every
# file under the page's src/ into one index.html, so no npm package is fetched:
#
#   CMake/WebEditor/sharedUiAndPreBuild  HooksPage: two SHARED_UI directories
#                                        merged into src/shared in order, and a
#                                        PRE_BUILD target's output, both in what
#                                        the bundler saw (CheckPage.cmake)
#   CMake/WebEditor/defaultsAddNothing   PlainPage: neither option, so no
#                                        src/shared and nothing but the page's
#                                        own files and the generated bindings
#
# Both cores are built into <build>/WebEditorFixtures and never installed.
if (NOT EACP_WEBVIEW_VITE_BUILD OR EACP_WEBVIEW_DEV)
    return()
endif ()

set(fixture_dir "${CMAKE_BINARY_DIR}/WebEditorFixtures")

foreach (fixture WebHooksFixture WebPlainFixture)
    makeasound_add_plugin(${fixture}
            FORMATS VST3
            OUTPUT_NAME ${fixture}
            BUNDLE_DIRECTORY "${fixture_dir}"
            NO_INSTALL
            FOLDER Tests/WebEditorBuild
            SOURCES Fixture.cpp)
endforeach ()

target_compile_definitions(WebHooksFixture PRIVATE FIXTURE_CODE="WbHk")
target_compile_definitions(WebPlainFixture PRIVATE FIXTURE_CODE="WbPl")

# The project's own codegen: a file CMake writes, copied into the page's src.
set(hooks_page "${CMAKE_CURRENT_SOURCE_DIR}/HooksPage")
set(page_info "${CMAKE_CURRENT_BINARY_DIR}/info.js")
set(page_info_output "${hooks_page}/src/build/info.js")

file(CONFIGURE OUTPUT "${page_info}"
        CONTENT "export const info = 'written by the PRE_BUILD step';\n")

add_custom_command(
        OUTPUT "${page_info_output}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${page_info}" "${page_info_output}"
        DEPENDS "${page_info}"
        VERBATIM)

add_custom_target(WebHooksFixturePageInfo DEPENDS "${page_info_output}")
set_target_properties(WebHooksFixturePageInfo PROPERTIES FOLDER Tests/WebEditorBuild)

makeasound_add_web_editor(WebHooksFixture
        WEB_DIR "${hooks_page}"
        SHARED_UI "${CMAKE_CURRENT_SOURCE_DIR}/SharedBase"
                  "${CMAKE_CURRENT_SOURCE_DIR}/SharedOverride"
        PRE_BUILD WebHooksFixturePageInfo)

makeasound_add_web_editor(WebPlainFixture
        WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PlainPage")

add_custom_target(WebEditorFixtures DEPENDS WebHooksFixture WebPlainFixture)
set_target_properties(WebEditorFixtures PROPERTIES FOLDER Tests/WebEditorBuild)
set(MAKEASOUND_WEB_EDITOR_FIXTURES WebEditorFixtures PARENT_SCOPE)

set(check_page "${CMAKE_CURRENT_SOURCE_DIR}/CheckPage.cmake")

add_test(NAME CMake/WebEditor/sharedUiAndPreBuild
        COMMAND ${CMAKE_COMMAND} -D CASE=hooks
                -D "PAGE=${hooks_page}"
                -D "DIST=${CMAKE_CURRENT_BINARY_DIR}/WebHooksFixture-WebHooksFixtureEditor-vite-dist"
                -D "SHARED=${CMAKE_CURRENT_SOURCE_DIR}"
                -P "${check_page}")

add_test(NAME CMake/WebEditor/defaultsAddNothing
        COMMAND ${CMAKE_COMMAND} -D CASE=plain
                -D "PAGE=${CMAKE_CURRENT_SOURCE_DIR}/PlainPage"
                -D "DIST=${CMAKE_CURRENT_BINARY_DIR}/WebPlainFixture-WebPlainFixtureEditor-vite-dist"
                -P "${check_page}")
