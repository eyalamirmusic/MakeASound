# Read by ctest after NanoTest's discovered entries (TEST_INCLUDE_FILES), so the
# case names are in MakeASoundTests_TESTS.
#
# A virtual MIDI port is system-wide (Core MIDI, the ALSA sequencer), and these
# cases open one under a fixed name, then find their own output by that name,
# a substring match. Two of them running at once under `ctest -j` can open each
# other's port, so they share one lock and run one at a time; every other case
# still runs in parallel.
set(virtual_midi_cases
        "^Midi/"
        "^Engine/loopbackMidiLandsInsideTheBlock$"
        "^Standalone/hardwareAndInjectedMidiReachThePluginInOrder$"
        "^Standalone/theEchoedMidiOutLeavesThroughTheSender$"
        "^Standalone/hostSendsThePluginsMidiOutThroughAVirtualOutput$"
        "^Standalone/hostReopensTheVirtualOutputItSaved$"
        "^Standalone/pollReopensAPickedInputPluggedBackIn$"
        "^Standalone/pollReopensAPickedOutputPluggedBackIn$"
        "^Allocations/theMidiInputThreadStaysOffTheHeap$"
        "^Allocations/aLongSysExOnTheMidiThreadStaysOffTheHeap$"
        "^Allocations/theOversizeSysExDropTouchesNothing$"
        "^Allocations/drainingRealQueuedMidiTouchesNothing$")

foreach (case IN LISTS MakeASoundTests_TESTS)
    foreach (pattern IN LISTS virtual_midi_cases)
        if (case MATCHES "${pattern}")
            set_tests_properties("${case}" PROPERTIES RESOURCE_LOCK VirtualMidiPorts)
            break()
        endif ()
    endforeach ()
endforeach ()
