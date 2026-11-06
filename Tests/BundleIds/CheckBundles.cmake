# cmake -D DIR=<build>/BundleIdFixtures -D FORMATS=VST3,AU[,AAX] -P CheckBundles.cmake
#
# Reads the bundles BundleIdOverride and BundleIdDefault were built into (see
# CMakeLists.txt here) and checks, per format, the Info.plist's
# CFBundleIdentifier, that the signature is valid, and that the identifier it was
# signed under is the same one: the overrides for the first, the
# <BUNDLE_ID>.<suffix> defaults for the second.
string(REPLACE "," ";" FORMATS "${FORMATS}")

set(VST3_folder VST3)
set(VST3_extension vst3)
set(VST3_override com.tamberplug.BundleIdOverride.vst3plugin)
set(VST3_default com.makeasound.bundleiddefault.vst3)

set(AU_folder AU)
set(AU_extension component)
set(AU_override com.tamberplug.BundleIdOverride.au)
set(AU_default com.makeasound.bundleiddefault.component)

set(AAX_folder AAX)
set(AAX_extension aaxplugin)
set(AAX_override com.tamberplug.BundleIdOverride.aax)
set(AAX_default com.makeasound.bundleiddefault.aaxplugin)

function(check_bundle bundle expected)
    if (NOT EXISTS "${bundle}/Contents/Info.plist")
        message(SEND_ERROR "${bundle}: no Info.plist")
        return()
    endif ()

    execute_process(
            COMMAND plutil -extract CFBundleIdentifier raw
                    "${bundle}/Contents/Info.plist"
            OUTPUT_VARIABLE plist_id
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE result)

    if (NOT result EQUAL 0 OR NOT plist_id STREQUAL expected)
        message(SEND_ERROR "${bundle}: CFBundleIdentifier is '${plist_id}', "
                "expected '${expected}'")
    endif ()

    execute_process(COMMAND codesign --verify --strict "${bundle}"
            ERROR_VARIABLE verify_output
            RESULT_VARIABLE result)

    if (NOT result EQUAL 0)
        message(SEND_ERROR "${bundle}: invalid signature: ${verify_output}")
    endif ()

    # codesign -d writes what it displays to stderr.
    execute_process(COMMAND codesign -d --verbose=2 "${bundle}"
            ERROR_VARIABLE display
            RESULT_VARIABLE result)
    string(REGEX MATCH "(^|\n)Identifier=([^\n]*)" match "${display}")
    set(signed_id "${CMAKE_MATCH_2}")

    if (NOT signed_id STREQUAL expected)
        message(SEND_ERROR "${bundle}: signed as '${signed_id}', expected '${expected}'")
    endif ()
endfunction()

foreach (format IN LISTS FORMATS)
    set(folder "${DIR}/${${format}_folder}")
    set(extension "${${format}_extension}")
    check_bundle("${folder}/BundleIdOverride.${extension}" "${${format}_override}")
    check_bundle("${folder}/BundleIdDefault.${extension}" "${${format}_default}")
endforeach ()
