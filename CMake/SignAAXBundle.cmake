# cmake -D WRAPTOOL=<path> [-D ACCOUNT=<iLok account>] -D SIGN_ID=<identity>
#       -D WCGUID=<guid> -D BUNDLE=<.aaxplugin> [-D TIMEOUT=<seconds>] [-D REQUIRED=ON]
#       -P SignAAXBundle.cmake
# Windows: cmake -D WRAPTOOL=<wraptool.exe> [-D ACCOUNT=<iLok account>]
#       -D SIGNTOOL=<signtool.exe>
#       (-D SIGN_ID=<thumbprint> | -D SIGNING_DLIB=<dlib> -D SIGNING_METADATA=<json>)
#       -D WCGUID=<guid> -D BUNDLE=<.aaxplugin> [-D VERIFY=<file>]
#       [-D TIMEOUT=<seconds>] [-D REQUIRED=ON] -P SignAAXBundle.cmake
#
# Has PACE's wraptool sign an AAX bundle in place, as ACCOUNT or, without one,
# wraptool's default account. No password: wraptool reads it from the keychain,
# where a sync done once outside the build saved it. Best effort: a bundle left
# with its ad-hoc signature still loads in Pro Tools Developer, so a failure is a
# warning, unless REQUIRED makes it an error. A wraptool run longer than TIMEOUT
# seconds (no limit when unset or 0) is stopped and fails the same way: wraptool
# can wait forever for input a build never gives it.
#
# SIGNTOOL is the Windows form: wraptool has that signtool add the Authenticode
# signature, through Microsoft Artifact Signing when SIGNING_DLIB and
# SIGNING_METADATA are given (wraptool hands signtool the whole command line and
# appends the file), else with the certificate whose SHA-1 thumbprint is SIGN_ID.
# VERIFY then has wraptool check that file, the DLL inside the bundle.

set(artifact_signing OFF)

if (SIGNTOOL AND SIGNING_DLIB AND SIGNING_METADATA)
    set(artifact_signing ON)
endif ()

set(required_arguments WRAPTOOL WCGUID BUNDLE)

if (NOT artifact_signing)
    list(APPEND required_arguments SIGN_ID)
endif ()

foreach (argument ${required_arguments})
    if (NOT ${argument})
        message(FATAL_ERROR "SignAAXBundle: ${argument} is required")
    endif ()
endforeach ()

set(failure WARNING)

if (REQUIRED)
    set(failure FATAL_ERROR)
endif ()

set(timeout)

if (TIMEOUT GREATER 0)
    set(timeout TIMEOUT ${TIMEOUT})
endif ()

get_filename_component(bundle_name "${BUNDLE}" NAME)

# A result that is no exit code is execute_process's text for a process it
# stopped, which names the timeout.
function(report_failure step result output)
    if (result MATCHES "timeout")
        message(${failure} "wraptool timed out after ${TIMEOUT} s trying to ${step} "
                "${bundle_name} and was stopped:\n${output}")
    else ()
        message(${failure} "wraptool could not ${step} ${bundle_name} (exit ${result}):\n${output}")
    endif ()
endfunction()

set(account)
if (ACCOUNT)
    set(account --account "${ACCOUNT}")
endif ()

set(signing --signid "${SIGN_ID}")

if (artifact_signing)
    set(signing --signtool "${SIGNTOOL}" --explicitsigningoptions
            "sign /v /fd SHA256 /tr http://timestamp.acs.microsoft.com /td SHA256 /dlib \"${SIGNING_DLIB}\" /dmdf \"${SIGNING_METADATA}\"")
elseif (SIGNTOOL)
    set(signing --signtool "${SIGNTOOL}" --signid "${SIGN_ID}"
            --extrasigningoptions digest_sha256)
endif ()

execute_process(
        COMMAND "${WRAPTOOL}" sign --verbose
                --autoinstall on
                ${account}
                --wcguid "${WCGUID}"
                ${signing}
                --in "${BUNDLE}"
                --out "${BUNDLE}"
        ${timeout}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)

if (NOT result EQUAL 0)
    report_failure(sign "${result}" "${output}")
    return()
endif ()

if (VERIFY)
    execute_process(
            COMMAND "${WRAPTOOL}" verify --verbose --in "${VERIFY}"
            ${timeout}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)

    if (NOT result EQUAL 0)
        report_failure(verify "${result}" "${output}")
        return()
    endif ()
endif ()

message(STATUS "PACE signed ${bundle_name}")
