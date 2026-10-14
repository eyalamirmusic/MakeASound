set(CMAKE_CXX_STANDARD 20)
set(CMAKE_FOLDER Tests)

find_package(NanoTest REQUIRED)

# MakeASoundCoreTests: MakeASoundCore linked into an executable on its own, built
# whether or not the device library is. The link proves no core TU needs a device
# symbol; the walk below proves nothing the core links brings a backend in.
function(_makeasound_check_core_links target)
    get_target_property(links ${target} LINK_LIBRARIES)
    get_target_property(interfaceLinks ${target} INTERFACE_LINK_LIBRARIES)

    foreach (link IN LISTS links interfaceLinks)
        if (link MATCHES "^(MakeASound|miniaudio|ALSA::ALSA|winmm)$"
                OR link MATCHES "CoreAudio|CoreMIDI|AudioToolbox|AVFoundation")
            message(FATAL_ERROR "MakeASoundCore links a device backend: ${link} "
                    "(through ${target})")
        endif ()

        if (TARGET ${link})
            get_target_property(aliased ${link} ALIASED_TARGET)

            if (aliased)
                set(link ${aliased})
            endif ()

            if (NOT link IN_LIST _makeasound_core_walked)
                list(APPEND _makeasound_core_walked ${link})
                set(_makeasound_core_walked ${_makeasound_core_walked} PARENT_SCOPE)
                _makeasound_check_core_links(${link})
                set(_makeasound_core_walked ${_makeasound_core_walked} PARENT_SCOPE)
            endif ()
        endif ()
    endforeach ()
endfunction()

_makeasound_check_core_links(MakeASoundCore)

nano_add_executable(MakeASoundCoreTests
        SOURCES CoreTests.cpp
        TARGETS MakeASoundCore)
set_target_properties(MakeASoundCoreTests PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

# The main suite covers the device library, so a core-only build stops here.
if (NOT TARGET MakeASound)
    message(STATUS "MakeASoundTests skipped (needs MAKEASOUND_BUILD_DEVICES)")
    return()
endif ()

set(MAKEASOUND_TEST_SOURCES
        SPSCQueueTests.cpp
        BufferTests.cpp
        EngineTests.cpp
        DeviceInfoTests.cpp
        DeviceManagerTests.cpp
        MidiParserTests.cpp
        MidiManagerTests.cpp
        RealtimeTests.cpp
        TestSynthTests.cpp)

set(MAKEASOUND_TEST_TARGETS MakeASound MakeASoundDSP)

# A Debug assertion on Windows otherwise opens a dialog, which a CI runner never
# dismisses; this TU routes it to stderr and aborts, so a case fails instead.
if (WIN32)
    list(APPEND MAKEASOUND_TEST_SOURCES AssertReporting-Windows.cpp)
endif ()

if (MAKEASOUND_BUILD_PLUGIN)
    list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundPlugin)
    list(APPEND MAKEASOUND_TEST_SOURCES
            ParameterTests.cpp
            IdPathTests.cpp
            StateTests.cpp
            PluginTests.cpp
            EditorTests.cpp
            JuceCompatTests.cpp
            JuceIdentityTests.cpp
            TamberPlugStateTests.cpp)

    # Every id a host stores, against the goldens in Identity/. Each format built
    # adds its own lines (IdentityManifest.h).
    list(APPEND MAKEASOUND_TEST_SOURCES IdentityManifest.cpp TamberPlugIdentityTests.cpp)
    set(MAKEASOUND_TEST_IDENTITY ON)

    # eacp-core so the swap's and the wrapper's cases can pump the message loop.
    list(APPEND MAKEASOUND_TEST_SOURCES RealtimeSwapTests.cpp)
    list(APPEND MAKEASOUND_TEST_TARGETS eacp-core)

    # The vendored VST3 SDK, plugin and host side: compiled and linked here before
    # any adapter or host needs it.
    list(APPEND MAKEASOUND_TEST_SOURCES VST3SDKTests.cpp)
    list(APPEND MAKEASOUND_TEST_TARGETS vst3sdk vst3sdk-hosting)

    # The module loader, which vst3sdk-hosting has everywhere but iOS.
    if (NOT IOS)
        list(APPEND MAKEASOUND_TEST_SOURCES VST3SDKLoaderTests.cpp)
    endif ()

    # The generic editor and the resize grip, and the editor every format suite
    # can show in place of a plugin's own.
    if (TARGET MakeASoundPluginUI)
        list(APPEND MAKEASOUND_TEST_SOURCES PluginUITests.cpp TestEditor.cpp)
        list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundPluginUI)
    endif ()

    # The standalone core, wherever the plugin core builds, and the default app's
    # window-tier pieces where the app was built.
    if (TARGET MakeASoundStandaloneCore)
        list(APPEND MAKEASOUND_TEST_SOURCES StandaloneTests.cpp)
        list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundStandaloneCore)
    endif ()

    if (TARGET MakeASoundStandalone)
        list(APPEND MAKEASOUND_TEST_SOURCES StandaloneUITests.cpp)
        list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundStandalone)
    endif ()

    # The VST3 adapter hosted in-process, with the SDK's own queues and event lists.
    if (TARGET MakeASoundVST3)
        list(APPEND MAKEASOUND_TEST_SOURCES VST3Tests.cpp IdentityManifestVST3.cpp)
        list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundVST3 vst3sdk-hosting)

        # A ViewRect is pixels on Windows, and an unscaled view follows the DPI.
        if (WIN32 AND TARGET MakeASoundPluginUI AND TARGET eacp-graphics)
            list(APPEND MAKEASOUND_TEST_SOURCES VST3ViewTests-Windows.cpp)
        endif ()
    endif ()

    # The AU adapter hosted in-process through AudioToolbox's C API. The suites
    # stay C++20 and include no adapter header; the module's entry point and view,
    # which include the C++23 SDK, build into a test-only archive that links the
    # format privately, so its standard does not reach the suites.
    if (TARGET MakeASoundAU)
        get_target_property(au_entry MakeASoundAU MAKEASOUND_AU_ENTRY)
        get_target_property(au_view_source MakeASoundAU MAKEASOUND_AU_VIEW_SOURCE)
        get_target_property(au_view_libraries MakeASoundAU MAKEASOUND_AU_VIEW_LIBRARIES)

        # AUTestPortFactory.cpp: the factory over JucePort.h's JUCE-scheme
        # descriptions, an adapter of the module's built over a description of
        # the tests' own.
        add_library(MakeASoundAUTestModule STATIC
                "${au_entry}" "${au_view_source}" AUTestPortFactory.cpp)
        target_link_libraries(MakeASoundAUTestModule PRIVATE
                MakeASoundAU ${au_view_libraries})
        target_compile_definitions(MakeASoundAUTestModule PRIVATE
                MAKEASOUND_AU_VIEW_CLASS=MakeASoundAUView_MakeASoundTests)
        target_compile_options(MakeASoundAUTestModule PRIVATE
                $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
        set_target_properties(MakeASoundAUTestModule PROPERTIES
                OBJCXX_STANDARD 23
                INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

        list(APPEND MAKEASOUND_TEST_SOURCES AUTests.cpp)
        list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundAUTestModule MakeASoundAUDescribe)

        # The view's size policy and placement, opened through its factory class.
        if (au_view_source MATCHES "CocoaUI\\.mm$")
            set(MAKEASOUND_TEST_AU_VIEW ON)
            list(APPEND MAKEASOUND_TEST_SOURCES AUViewTests.mm)
            set_source_files_properties(AUViewTests.mm PROPERTIES
                    COMPILE_OPTIONS -fobjc-arc)
        endif ()
    endif ()

    # Both AU plist forms, on every Apple platform.
    if (TARGET MakeASoundAUDescribe)
        list(APPEND MAKEASOUND_TEST_SOURCES AUPlistTests.cpp IdentityManifestAU.cpp)
        list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundAUDescribe)
    endif ()

    # The AUv3 adapter, its AUAudioUnit subclass registered in process, on macOS and
    # in the iOS simulator. The sources every module compiles build into the suite
    # under a class suffix of the tests' own; the instantiation option is the one
    # thing that differs per platform.
    if (TARGET MakeASoundAUv3)
        get_target_property(auv3_sources MakeASoundAUv3 MAKEASOUND_AUV3_SOURCES)
        get_target_property(auv3_view_libraries MakeASoundAUv3
                MAKEASOUND_AUV3_VIEW_LIBRARIES)
        get_target_property(auv3_has_view MakeASoundAUv3 MAKEASOUND_AUV3_HAS_VIEW)

        if (IOS)
            set(auv3_host AUv3TestHost-iOS.mm)
            set(auv3_view_host AUv3ViewTestHost-iOS.mm)
        else ()
            set(auv3_host AUv3TestHost-macOS.mm)
            set(auv3_view_host AUv3ViewTestHost-macOS.mm)
        endif ()

        set(MAKEASOUND_TEST_AUV3_SOURCES AUv3Tests.mm ${auv3_host} ${auv3_sources})

        # The view controller, the extension's principal class, over the
        # platform's view framework.
        if (auv3_has_view)
            list(APPEND MAKEASOUND_TEST_AUV3_SOURCES
                    AUv3ViewTests.mm ${auv3_view_host})
        endif ()

        # The AUv3's saved state restored by the v2 component, where both exist.
        if (TARGET MakeASoundAU)
            list(APPEND MAKEASOUND_TEST_AUV3_SOURCES AUStateInteropTests.mm)
        endif ()

        list(APPEND MAKEASOUND_TEST_SOURCES ${MAKEASOUND_TEST_AUV3_SOURCES})
        list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundAUv3 ${auv3_view_libraries})
    endif ()

    # The AAX adapter hosted in-process over fakes of the SDK's host interfaces.
    if (TARGET MakeASoundAAX)
        list(APPEND MAKEASOUND_TEST_SOURCES AAXTests.cpp IdentityManifestAAX.cpp)
        list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundAAX)
    endif ()

    # The AAX view over a fake view container, where the view is built.
    if (TARGET MakeASoundAAX AND TARGET MakeASoundPluginUI AND TARGET eacp-graphics)
        list(APPEND MAKEASOUND_TEST_SOURCES AAXViewTests.cpp)
        list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundAAX)
    endif ()

    # Hosting third-party plugins: the scan pipeline and the cache. The test binary
    # links the library directly rather than through makeasound_enable_hosting,
    # whose sealed exports would hide the allocation suites' operator new from the
    # system libraries.
    if (TARGET MakeASoundHosting)
        list(APPEND MAKEASOUND_TEST_SOURCES HostingCacheTests.cpp HostingScanTests.cpp)
        list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundHosting)

        # A stand-in scan worker whose misbehaviour the command line picks
        # (crash, abort, hang, garbage, the old marker), for the scan suites.
        add_executable(FakeScanWorker FakeScanWorker.cpp)
        makeasound_enable_hosting(FakeScanWorker)

        # Its abort, and the crash fixture's, must end the process rather than
        # open a dialog no CI runner dismisses.
        if (WIN32)
            target_sources(FakeScanWorker PRIVATE AssertReporting-Windows.cpp)
        endif ()
        set_makeasound_target_settings(FakeScanWorker)
        set(MAKEASOUND_TEST_FAKE_SCAN_WORKER ON)

        # Real VST3 modules to scan: the fixtures, built beside the suite.
        if (TARGET MakeASoundVST3)
            add_subdirectory(HostFixtures)
            list(APPEND MAKEASOUND_TEST_SOURCES HostingVST3ScanTests.cpp HostedVST3Tests.cpp)
            set(MAKEASOUND_TEST_HOST_FIXTURES ON)

            # Apps/PluginScanner, run as a program over the fixtures.
            if (TARGET PluginScanner)
                list(APPEND MAKEASOUND_TEST_SOURCES HostingScannerAppTests.cpp)
                set(MAKEASOUND_TEST_PLUGIN_SCANNER ON)
            endif ()

            # Apps/PluginHost: its Host headless, and the app as a scan worker.
            if (TARGET PluginHostCore)
                list(APPEND MAKEASOUND_TEST_SOURCES HostingPluginHostTests.cpp)
                list(APPEND MAKEASOUND_TEST_TARGETS PluginHostCore)
            endif ()

            if (TARGET PluginHost)
                list(APPEND MAKEASOUND_TEST_SOURCES HostingPluginHostAppTests.cpp)
                set(MAKEASOUND_TEST_PLUGIN_HOST ON)
            endif ()
        endif ()

        # Audio Units: Apple's own, found through the registry, and the test
        # module's own components, registered here and loaded directly.
        if (APPLE)
            list(APPEND MAKEASOUND_TEST_SOURCES HostingAUScanTests.cpp)

            if (TARGET MakeASoundAU)
                list(APPEND MAKEASOUND_TEST_SOURCES HostedAUTests.cpp)
            endif ()

            # Hosted editors embedded in a plain NSView.
            if (TARGET MakeASoundHostedEditor AND TARGET MakeASoundAU
                    AND MAKEASOUND_TEST_HOST_FIXTURES)
                list(APPEND MAKEASOUND_TEST_SOURCES HostedEditorTests.mm)
                list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundHostedEditor)
                set_source_files_properties(HostedEditorTests.mm PROPERTIES
                        COMPILE_OPTIONS -fobjc-arc)
            endif ()
        endif ()
    endif ()

    # pluginval and auval over the example bundles, run only when MAKEASOUND_PLUGINVAL
    # is set.
    if (TARGET MakeASoundPluginval AND TARGET MakeASoundVST3)
        list(APPEND MAKEASOUND_TEST_SOURCES PluginvalTests.cpp)
        list(APPEND MAKEASOUND_TEST_TARGETS MakeASoundPluginval)
        set(MAKEASOUND_TEST_PLUGINVAL ON)
    endif ()

    # makeasound_add_plugin's bundle identifiers: CMake-level cases, and on macOS
    # two fixtures' built bundles.
    add_subdirectory(BundleIds)
endif ()

# The ALSA backend's conversions are the one half of a platform backend that can
# be measured without the platform: alsa-lib turns events into bytes and back
# with no sequencer open, so these run on a container where the loopback below
# has nothing to open.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(ALSA REQUIRED)

    list(APPEND MAKEASOUND_TEST_SOURCES ALSABackendTests.cpp)
    list(APPEND MAKEASOUND_TEST_TARGETS ALSA::ALSA)
endif ()

# The allocation suites interpose malloc/new through dlsym(RTLD_NEXT, ...), which
# only exists on Apple and Linux. Elsewhere the checker degrades to a stub, so the
# suites are left out of the build rather than compiled into something that reports
# zero allocations because nothing was watching.
#
# Test-only: linking AllocationsChecker compiles the interposers into this
# executable and nothing else. See CMake/FindScopedMemoryAllocations.cmake.
#
# No CI runner has MIDI hardware either, so the input path is measured through a
# virtual-port loopback: our own output sending into our own virtual input, which
# is a real platform round trip on macOS, iOS and Linux. Windows has no virtual
# ports, so there its input path is covered by the parser tests alone and every
# loopback case returns without asserting. A Linux box with no sequencer -
# a container with no /dev/snd/seq - is in the same position, and skips the same
# way rather than failing.
if (APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(ScopedMemoryAllocations REQUIRED)

    list(APPEND MAKEASOUND_TEST_SOURCES
            AllocationTests.cpp
            RealtimeThreadAllocationTests.cpp)

    list(APPEND MAKEASOUND_TEST_TARGETS AllocationsChecker)

    if (MAKEASOUND_BUILD_PLUGIN)
        list(APPEND MAKEASOUND_TEST_SOURCES PluginAllocationTests.cpp)
    endif ()

    if (TARGET MakeASoundStandaloneCore)
        list(APPEND MAKEASOUND_TEST_SOURCES StandaloneAllocationTests.cpp)
    endif ()

    if (TARGET MakeASoundVST3)
        list(APPEND MAKEASOUND_TEST_SOURCES VST3AllocationTests.cpp)
    endif ()

    if (TARGET MakeASoundAU)
        list(APPEND MAKEASOUND_TEST_SOURCES AUAllocationTests.cpp)
    endif ()

    if (TARGET MakeASoundAUv3)
        list(APPEND MAKEASOUND_TEST_SOURCES AUv3AllocationTests.mm)
        list(APPEND MAKEASOUND_TEST_AUV3_SOURCES AUv3AllocationTests.mm)
    endif ()

    if (TARGET MakeASoundAAX)
        list(APPEND MAKEASOUND_TEST_SOURCES AAXAllocationTests.cpp)
    endif ()

    if (MAKEASOUND_TEST_HOST_FIXTURES)
        list(APPEND MAKEASOUND_TEST_SOURCES HostingAllocationTests.cpp)
    endif ()

    if (TARGET MakeASoundHosting AND TARGET MakeASoundAU)
        list(APPEND MAKEASOUND_TEST_SOURCES HostingAUAllocationTests.cpp)
    endif ()
endif ()

# NanoTest registers one ctest case per nano::test() and re-runs the binary with
# --test <name>, so `ctest` lists each case individually. Link MakeASound so the
# suite can grow to cover the whole public surface, not just the header-only
# pieces.
nano_add_executable(MakeASoundTests
        SOURCES ${MAKEASOUND_TEST_SOURCES}
        TARGETS ${MAKEASOUND_TEST_TARGETS})

# Release LTO as every target of ours has: eacp's Release archives are LTO bitcode
# under Clang, which a link without -flto cannot read.
set_target_properties(MakeASoundTests PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

if (MAKEASOUND_TEST_AUV3_SOURCES)
    set_source_files_properties(${MAKEASOUND_TEST_AUV3_SOURCES} PROPERTIES
            COMPILE_OPTIONS -fobjc-arc
            COMPILE_DEFINITIONS MAKEASOUND_AUV3_CLASS_SUFFIX=MakeASoundTests)
endif ()

if (MAKEASOUND_TEST_FAKE_SCAN_WORKER)
    add_dependencies(MakeASoundTests FakeScanWorker)
    target_compile_definitions(MakeASoundTests PRIVATE
            MAKEASOUND_FAKE_SCAN_WORKER="$<TARGET_FILE:FakeScanWorker>")
endif ()

if (MAKEASOUND_TEST_HOST_FIXTURES)
    add_dependencies(MakeASoundTests
            HostFixtureEffect-VST3 HostFixtureInstrument-VST3
            HostFixtureScanCrash-VST3 HostFixtureScanHang-VST3)
    target_compile_definitions(MakeASoundTests PRIVATE
            MAKEASOUND_HOST_FIXTURE_VST3_DIR="${MAKEASOUND_HOST_FIXTURE_VST3_DIR}")

    if (TARGET HostFixtureEffect-AU)
        add_dependencies(MakeASoundTests
                HostFixtureEffect-AU HostFixtureInstrument-AU)
        target_compile_definitions(MakeASoundTests PRIVATE
                MAKEASOUND_HOST_FIXTURE_AU_DIR="${MAKEASOUND_HOST_FIXTURE_AU_DIR}")
    endif ()
endif ()

if (MAKEASOUND_BUNDLE_ID_FIXTURES)
    add_dependencies(MakeASoundTests ${MAKEASOUND_BUNDLE_ID_FIXTURES})
endif ()

if (MAKEASOUND_TEST_PLUGIN_SCANNER)
    add_dependencies(MakeASoundTests PluginScanner)
    target_compile_definitions(MakeASoundTests PRIVATE
            MAKEASOUND_PLUGIN_SCANNER="$<TARGET_FILE:PluginScanner>")
endif ()

if (MAKEASOUND_TEST_PLUGIN_HOST)
    add_dependencies(MakeASoundTests PluginHost)
    target_compile_definitions(MakeASoundTests PRIVATE
            MAKEASOUND_PLUGIN_HOST="$<TARGET_FILE:PluginHost>")
endif ()

if (MAKEASOUND_TEST_AU_VIEW)
    target_compile_definitions(MakeASoundTests PRIVATE MAKEASOUND_AU_HAS_VIEW=1)
endif ()

if (MAKEASOUND_TEST_IDENTITY)
    target_compile_definitions(MakeASoundTests PRIVATE
            MAKEASOUND_TEST_IDENTITY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Identity")
endif ()

if (MAKEASOUND_TEST_PLUGINVAL)
    target_compile_definitions(MakeASoundTests PRIVATE
            MAKEASOUND_VST3_DIR="${CMAKE_BINARY_DIR}/VST3")

    if (TARGET MakeASoundAU)
        target_compile_definitions(MakeASoundTests PRIVATE
                MAKEASOUND_AU_DIR="${CMAKE_BINARY_DIR}/AU")
    endif ()
endif ()
