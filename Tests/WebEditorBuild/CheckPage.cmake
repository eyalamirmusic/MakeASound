# cmake -D CASE=hooks|plain -D PAGE=<page dir> -D DIST=<the build's dist>
#       [-D SHARED=<this folder>] -P CheckPage.cmake
#
# Reads the index.html bundle.mjs wrote, every file the page's build saw under
# src/ with its text, and checks what makeasound_add_web_editor put there: the
# generated bindings always; for `hooks` the merged SHARED_UI copy and the
# PRE_BUILD step's file as well, and for `plain` neither.
set(bundle "${DIST}/index.html")

if (NOT EXISTS "${bundle}")
    message(FATAL_ERROR "${bundle}: the page's build wrote nothing")
endif ()

file(READ "${bundle}" seen)

function(expect text why)
    string(FIND "${seen}" "${text}" at)

    if (at EQUAL -1)
        message(SEND_ERROR "the build did not see ${why} ('${text}')")
    endif ()
endfunction()

function(expect_not text why)
    string(FIND "${seen}" "${text}" at)

    if (NOT at EQUAL -1)
        message(SEND_ERROR "the build saw ${why} ('${text}')")
    endif ()
endfunction()

# The schema codegen ran before the bundler, in both.
expect("=== generated/schema.hooks.ts" "the generated hooks")
expect("useWebParameterValue" "the keyed parameter hooks")
expect("=== main.js" "the page's own file")

if (CASE STREQUAL "hooks")
    expect("=== shared/common.js\nexport const common = 'from SharedBase';"
            "SharedBase's file")
    expect("=== shared/looks.js\nexport const looks = 'the SharedOverride looks';"
            "SharedOverride's file in place of SharedBase's")
    expect_not("the SharedBase looks" "the file SharedOverride replaced")
    expect("=== build/info.js\nexport const info = 'written by the PRE_BUILD step';"
            "the PRE_BUILD step's output")

    # What the page has is a copy, the same bytes.
    foreach (file SharedBase/common.js SharedOverride/looks.js)
        get_filename_component(leaf "${file}" NAME)
        file(SHA256 "${SHARED}/${file}" source)
        file(SHA256 "${PAGE}/src/shared/${leaf}" copy)

        if (NOT source STREQUAL copy)
            message(SEND_ERROR "src/shared/${leaf} is not a copy of ${file}")
        endif ()
    endforeach ()
elseif (CASE STREQUAL "plain")
    expect_not("=== shared/" "a shared directory with no SHARED_UI")
    expect_not("=== build/" "a pre-build output with no PRE_BUILD")

    if (EXISTS "${PAGE}/src/shared")
        message(SEND_ERROR "${PAGE}/src/shared exists with no SHARED_UI")
    endif ()
else ()
    message(FATAL_ERROR "CheckPage.cmake: unknown CASE '${CASE}'")
endif ()
