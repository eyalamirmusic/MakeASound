# cmake -D WRAPTOOL=<path> [-D ACCOUNT=<iLok account>] -D SIGN_ID=<identity>
#       -D WCGUID=<guid> -D BUNDLE=<.aaxplugin> -P SignAAXBundle.cmake
#
# Has PACE's wraptool sign an AAX bundle in place, as ACCOUNT or, without one,
# wraptool's default account. No password: wraptool reads it from the keychain,
# where a sync done once outside the build saved it. Best effort: a bundle left
# with its ad-hoc signature still loads in Pro Tools Developer, so a failure is a
# warning.

foreach (argument WRAPTOOL SIGN_ID WCGUID BUNDLE)
    if (NOT ${argument})
        message(FATAL_ERROR "SignAAXBundle: ${argument} is required")
    endif ()
endforeach ()

get_filename_component(bundle_name "${BUNDLE}" NAME)

set(account)
if (ACCOUNT)
    set(account --account "${ACCOUNT}")
endif ()

execute_process(
        COMMAND "${WRAPTOOL}" sign --verbose
                --autoinstall on
                ${account}
                --wcguid "${WCGUID}"
                --signid "${SIGN_ID}"
                --in "${BUNDLE}"
                --out "${BUNDLE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)

if (result EQUAL 0)
    message(STATUS "PACE signed ${bundle_name}")
else ()
    message(WARNING "wraptool could not sign ${bundle_name} (exit ${result}):\n${output}")
endif ()
