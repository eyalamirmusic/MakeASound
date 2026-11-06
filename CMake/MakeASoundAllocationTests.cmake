# makeasound_add_allocation_tests(<plugin> [TEST_PREFIX <prefix>])
#
# Runs a plugin made by makeasound_add_plugin through MakeASound's per-format
# allocation suites: the format-generic half of each (Tests/*AllocationDriver.h),
# over every class of the plugin's describeModule(), with the heap banned around
# each measured call. One executable, <plugin>-MakeASoundAllocationTests,
# linking the plugin's static core (so its describeModule(), one plugin per
# executable), AllocationsChecker, NanoTest and an in-process host for each of
# the plugin's FORMATS whose library this tree built:
#
#   VST3        the adapter driven as a DAW drives it: busy blocks of automation,
#               notes, mapped controllers and a transport, reconfigured and
#               offline; setParamNormalized; the plugin's own moves; a held edit.
#   AU          the module's AU entry built into a test archive and registered
#               in process, driven through AudioToolbox: busy renders, re-init,
#               held parameters, the plugin's own moves (macOS).
#   AUv3        the AUAudioUnit subclass compiled in under a class suffix of the
#               executable's, its internal render block with parameter and MIDI
#               events, re-allocated, held (Apple; on macOS too, where makeasound_
#               add_plugin builds no AUv3).
#   AAX         the adapter over fakes of Pro Tools' interfaces, on the first stem
#               pair the plugin accepts, in place and not, held (with
#               MAKEASOUND_BUILD_AAX).
#   Standalone  a StandaloneProcessor block on Engine.
#   Hosting     with MakeASoundHosting: the plugin's own built VST3 bundle, and
#               its Audio Units registered in process, loaded as HostedPlugins
#               and given busy blocks; the host side is measured with the plugin.
#
# Every case is one NanoTest case per format and path that loops over the
# classes (a failure names the class); ctest lists them as <prefix><Format>/<case>,
# prefix default "<plugin>.". Only on Apple and Linux, where the checker can
# interpose malloc: elsewhere this says so and does nothing.
#
# A consumer finds NanoTest and ScopedMemoryAllocations first, as MakeASound's
# own tests do (CMake/FindNanoTest.cmake, CMake/FindScopedMemoryAllocations.cmake;
# both test-only, never in a shipped binary). Defined whenever MakeASoundPlugin
# is, so it works with MakeASound as a CPM subdirectory, not only top level: the
# sources are read from this tree's Tests/ by path, which needs no Tests/ build.

function(makeasound_add_allocation_tests plugin)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "TEST_PREFIX" "")

    if (ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "makeasound_add_allocation_tests(${plugin}): unknown "
                "arguments ${ARG_UNPARSED_ARGUMENTS}")
    endif ()

    if (NOT TARGET ${plugin})
        message(FATAL_ERROR "makeasound_add_allocation_tests(${plugin}): no such target")
    endif ()

    get_target_property(formats ${plugin} MAKEASOUND_FORMATS)

    if (NOT formats)
        message(FATAL_ERROR "makeasound_add_allocation_tests(${plugin}): not a "
                "makeasound_add_plugin target (it records no MAKEASOUND_FORMATS)")
    endif ()

    if (NOT (APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
        message(STATUS "${plugin}: MakeASound allocation tests skipped (the "
                "checker interposes malloc only on Apple and Linux)")
        return()
    endif ()

    if (NOT TARGET AllocationsChecker OR NOT COMMAND nano_add_executable)
        message(FATAL_ERROR "makeasound_add_allocation_tests(${plugin}): needs "
                "AllocationsChecker and nano_add_executable; "
                "find_package(ScopedMemoryAllocations) and find_package(NanoTest) "
                "first")
    endif ()

    if (NOT DEFINED ARG_TEST_PREFIX)
        set(ARG_TEST_PREFIX "${plugin}.")
    endif ()

    cmake_path(SET dir NORMALIZE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Tests")
    set(target ${plugin}-MakeASoundAllocationTests)
    string(MAKE_C_IDENTIFIER "${plugin}" id)
    set(sources "")
    set(links ${plugin} AllocationsChecker)
    set(definitions "")

    if ("VST3" IN_LIST formats AND TARGET MakeASoundVST3)
        list(APPEND sources "${dir}/PluginAllocations/VST3.cpp")
        list(APPEND links MakeASoundVST3 vst3sdk-hosting)
    endif ()

    if ("Standalone" IN_LIST formats AND TARGET MakeASoundStandaloneCore)
        list(APPEND sources "${dir}/PluginAllocations/Standalone.cpp")
        list(APPEND links MakeASoundStandaloneCore)
    endif ()

    # The module's entry and view over the SDK, which is C++23, in an archive of
    # their own that links the format privately, so its standard stays out of the
    # suites; with the plugin-access TU the host asks for the instance through.
    set(au_module "")

    if ("AU" IN_LIST formats AND TARGET MakeASoundAU)
        get_target_property(au_entry MakeASoundAU MAKEASOUND_AU_ENTRY)
        get_target_property(au_view_source MakeASoundAU MAKEASOUND_AU_VIEW_SOURCE)
        get_target_property(au_view_libraries MakeASoundAU MAKEASOUND_AU_VIEW_LIBRARIES)

        set(au_module ${target}-AU)
        add_library(${au_module} STATIC
                "${au_entry}" "${au_view_source}" "${dir}/AUTestPluginAccess.cpp")
        target_link_libraries(${au_module} PRIVATE
                MakeASoundAU ${plugin} ${au_view_libraries})
        target_compile_definitions(${au_module} PRIVATE
                MAKEASOUND_AU_VIEW_CLASS=MakeASoundAUView_${id}_AllocationTests)
        target_compile_options(${au_module} PRIVATE
                $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
        set_target_properties(${au_module} PROPERTIES
                OBJCXX_STANDARD 23
                UNITY_BUILD OFF
                INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

        list(APPEND sources "${dir}/PluginAllocations/AU.cpp")
        list(APPEND links ${au_module} MakeASoundAUDescribe)
    endif ()

    # The subclass every module compiles, under this executable's class suffix:
    # the Objective-C runtime is one per process, but this is the only one here.
    set(has_objc_sources OFF)

    if ("AUv3" IN_LIST formats AND TARGET MakeASoundAUv3)
        get_target_property(auv3_sources MakeASoundAUv3 MAKEASOUND_AUV3_SOURCES)
        get_target_property(auv3_view_libraries MakeASoundAUv3
                MAKEASOUND_AUV3_VIEW_LIBRARIES)

        if (IOS)
            set(auv3_host "${dir}/AUv3TestHost-iOS.mm")
        else ()
            set(auv3_host "${dir}/AUv3TestHost-macOS.mm")
        endif ()

        list(APPEND sources "${dir}/PluginAllocations/AUv3.mm" "${auv3_host}"
                ${auv3_sources})
        list(APPEND links MakeASoundAUv3 MakeASoundAUDescribe ${auv3_view_libraries})
        list(APPEND definitions MAKEASOUND_AUV3_CLASS_SUFFIX=${id}_AllocationTests)
        set(has_objc_sources ON)
    endif ()

    if ("AAX" IN_LIST formats AND TARGET MakeASoundAAX)
        list(APPEND sources "${dir}/PluginAllocations/AAX.cpp")
        list(APPEND links MakeASoundAAX)
    endif ()

    # Linked directly rather than through makeasound_enable_hosting, whose sealed
    # exports would hide the checker's operator new from the system libraries.
    set(hosted_vst3 OFF)

    if (TARGET MakeASoundHosting)
        if (TARGET ${plugin}-VST3)
            get_target_property(vst3_bundle ${plugin}-VST3 MAKEASOUND_VST3_BUNDLE)
            list(APPEND sources "${dir}/PluginAllocations/HostedVST3.cpp")
            list(APPEND definitions
                    "MAKEASOUND_ALLOCATION_VST3_BUNDLE=\"${vst3_bundle}\"")
            set(hosted_vst3 ON)
        endif ()

        if (au_module)
            list(APPEND sources "${dir}/PluginAllocations/HostedAU.cpp")
        endif ()

        if (hosted_vst3 OR au_module)
            list(APPEND links MakeASoundHosting)
        endif ()
    endif ()

    if (NOT sources)
        message(STATUS "${plugin}: MakeASound allocation tests skipped (none of "
                "its formats, ${formats}, was built)")
        return()
    endif ()

    nano_add_executable(${target}
            SOURCES ${sources}
            TARGETS ${links}
            TEST_PREFIX "${ARG_TEST_PREFIX}")

    get_target_property(folder ${plugin} FOLDER)

    if (folder)
        set_target_properties(${target} PROPERTIES FOLDER "${folder}")
    endif ()

    # Release LTO as every target of ours has: eacp's Release archives are LTO
    # bitcode under Clang. Never a unity build: VST3Common.h must open the VST3 TU.
    set_target_properties(${target} PROPERTIES
            UNITY_BUILD OFF
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

    if (definitions)
        target_compile_definitions(${target} PRIVATE ${definitions})
    endif ()

    if (has_objc_sources)
        target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    endif ()

    if (hosted_vst3)
        add_dependencies(${target} ${plugin}-VST3)
    endif ()
endfunction()
