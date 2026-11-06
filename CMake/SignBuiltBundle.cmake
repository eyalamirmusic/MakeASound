# cmake -D BUNDLE=<bundle> -D EXECUTABLE=<name> -P SignBuiltBundle.cmake
#
# Signs an iOS app ad hoc once it has been linked, and does nothing before: an
# app extension built into the app's PlugIns folder re-signs the app over its new
# signature, which the app's own post-build cannot do when only the extension
# changed, and on a first build the app is signed after it links instead.
if (NOT EXISTS "${BUNDLE}/${EXECUTABLE}")
    return()
endif ()

execute_process(COMMAND codesign --force --sign - --timestamp=none "${BUNDLE}"
        RESULT_VARIABLE result)

if (NOT result EQUAL 0)
    message(FATAL_ERROR "codesign failed for ${BUNDLE}")
endif ()
