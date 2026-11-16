# Read by ctest after NanoTest's discovered entries (TEST_INCLUDE_FILES), so the
# case names are in MakeASoundTests_TESTS.
#
# On Linux the hosted editor cases share one X server, and
# HostedEditor/aVST3ViewFollowsTheWindowsScale sets Xft.dpi in its root's
# RESOURCE_MANAGER, which every X11 window's scale comes from. An editor case
# running beside it under `ctest -j` opens at scale 2 and sees set_scale or a
# set_size it never asked for, so the hosted editor cases share one lock and run
# one at a time; every other case still runs in parallel.
set(hosted_editor_cases
        "^HostedEditor/"
        "^HostedCLAPEditor/")

foreach (case IN LISTS MakeASoundTests_TESTS)
    foreach (pattern IN LISTS hosted_editor_cases)
        if (case MATCHES "${pattern}")
            set_tests_properties("${case}" PROPERTIES RESOURCE_LOCK HostedEditors)
            break()
        endif ()
    endforeach ()
endforeach ()
