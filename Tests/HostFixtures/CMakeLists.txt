# Fixture plugins for the hosting suites: real VST3 modules built by this tree,
# so no test needs a third-party plugin. Never installed, and built into
# <build>/HostFixtures rather than <build>/VST3, where a validator sweep of the
# examples would find them (and the crash fixture would end it).
#
#   HostFixtureEffect      stereo in and out plus a mono sidechain, a skewed
#                          parameter
#   HostFixtureInstrument  MIDI in and out, stereo out, a fixed-size editor that
#                          resizes itself (none without eacp graphics)
#   HostFixtureScanCrash   abort()s when the factory creates it
#   HostFixtureScanHang    sleeps past any scan timeout when created
#
# The crash and hang fixtures are scanned only out of process, through a worker:
# in the test process they would take the suite down with them.
#
# On macOS the effect and the instrument are Audio Units too, in
# <build>/HostFixtures/AU, which no AU registry looks in. A test loads the
# component's own factory out of the bundle and registers it in its process
# (AudioComponentRegister), so nothing is ever found through the registry.
#
# With MAKEASOUND_BUILD_CLAP all four are CLAP modules as well, in
# <build>/HostFixtures/CLAP, with HostFixturePair (two plugins in one module).
set(host_fixture_dir "${CMAKE_BINARY_DIR}/HostFixtures")

foreach (fixture Effect Instrument ScanCrash ScanHang)
    set(fixture_formats VST3)

    if (fixture MATCHES "^(Effect|Instrument)$" AND TARGET MakeASoundAU)
        list(APPEND fixture_formats AU)
    endif ()

    if (TARGET MakeASoundCLAP)
        list(APPEND fixture_formats CLAP)
    endif ()

    makeasound_add_plugin(HostFixture${fixture}
            FORMATS ${fixture_formats}
            OUTPUT_NAME "HostFixture${fixture}"
            BUNDLE_DIRECTORY "${host_fixture_dir}"
            NO_INSTALL
            FOLDER Tests/HostFixtures
            SOURCES ${fixture}.cpp)
endforeach ()

if (TARGET eacp-graphics)
    target_sources(HostFixtureInstrument PRIVATE InstrumentEditor.cpp)
    target_link_libraries(HostFixtureInstrument PRIVATE eacp-graphics)
else ()
    target_sources(HostFixtureInstrument PRIVATE NoInstrumentEditor.cpp)
endif ()

set(MAKEASOUND_HOST_FIXTURE_VST3_DIR "${host_fixture_dir}/VST3" PARENT_SCOPE)
set(MAKEASOUND_HOST_FIXTURE_AU_DIR "${host_fixture_dir}/AU" PARENT_SCOPE)

# With the CLAP format, the four fixtures are CLAP modules too, in
# <build>/HostFixtures/CLAP, beside HostFixturePair, one module holding two
# plugins, built as CLAP alone so the VST3 suites' fixture counts stand.
if (TARGET MakeASoundCLAP)
    makeasound_add_plugin(HostFixturePair
            FORMATS CLAP
            OUTPUT_NAME "HostFixturePair"
            BUNDLE_DIRECTORY "${host_fixture_dir}"
            NO_INSTALL
            FOLDER Tests/HostFixtures
            SOURCES Pair.cpp)

    set(MAKEASOUND_HOST_FIXTURE_CLAP_DIR "${host_fixture_dir}/CLAP" PARENT_SCOPE)
endif ()
