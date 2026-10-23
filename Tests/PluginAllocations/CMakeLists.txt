# The example plugins through makeasound_add_allocation_tests, as a consumer runs
# its own: an effect and an instrument, every format of theirs this tree built.
# A directory of its own so nothing here shares source properties with
# MakeASoundTests, which compiles some of the same format sources.
foreach (plugin IN ITEMS Gain SynthPlugin)
    if (TARGET ${plugin})
        makeasound_add_allocation_tests(${plugin} TEST_PREFIX "Allocations/${plugin}/")
    endif ()
endforeach ()
