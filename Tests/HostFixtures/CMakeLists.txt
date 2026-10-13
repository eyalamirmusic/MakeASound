# Fixture plugins for the hosting suites: real VST3 modules built by this tree,
# so no test needs a third-party plugin. Never installed, and built into
# <build>/HostFixtures rather than <build>/VST3, where a validator sweep of the
# examples would find them (and the crash fixture would end it).
#
#   HostFixtureEffect      stereo in and out plus a mono sidechain, a skewed
#                          parameter
#   HostFixtureInstrument  MIDI in and out, stereo out
#   HostFixtureScanCrash   abort()s when the factory creates it
#   HostFixtureScanHang    sleeps past any scan timeout when created
#
# The crash and hang fixtures are scanned only out of process, through a worker:
# in the test process they would take the suite down with them.
set(host_fixture_dir "${CMAKE_BINARY_DIR}/HostFixtures")

foreach (fixture Effect Instrument ScanCrash ScanHang)
    makeasound_add_plugin(HostFixture${fixture}
            FORMATS VST3
            OUTPUT_NAME "HostFixture${fixture}"
            BUNDLE_DIRECTORY "${host_fixture_dir}"
            NO_INSTALL
            FOLDER Tests/HostFixtures
            SOURCES ${fixture}.cpp)
endforeach ()

set(MAKEASOUND_HOST_FIXTURE_VST3_DIR "${host_fixture_dir}/VST3" PARENT_SCOPE)
