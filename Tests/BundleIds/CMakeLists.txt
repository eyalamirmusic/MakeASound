# makeasound_add_plugin's bundle identifiers, checked at the CMake level:
#
#   CMake/BundleIds/resolve           the defaults and the per-format overrides,
#                                     resolved in script mode (Resolve.cmake)
#   CMake/BundleIds/auv3ExtendsTheApp an AUV3_BUNDLE_ID outside BUNDLE_ID is an
#                                     error
#   CMake/BundleIds/carriedAUv3       an AUv3 carried by another plugin's app
#                                     (AUV3_APP) extends that app's identifier
#   CMake/BundleIds/carriedAUv3ExtendsItsApp  and one outside it is an error
#   CMake/BundleIds/builtBundles      on macOS, the CFBundleIdentifier and the
#                                     code signature's identifier of the bundles
#                                     two fixtures built (CheckBundles.cmake)
#
# BundleIdOverride replaces every format's identifier, the way a plugin ported
# from Tamber Plug keeps the ids its installed bundles shipped with;
# BundleIdDefault passes none, so the defaults are pinned too. Both are built
# into <build>/BundleIdFixtures, out of every folder a validator sweeps, and are
# never installed.
set(bundle_id_dir "${CMAKE_BINARY_DIR}/BundleIdFixtures")
set(bundle_id_formats VST3)

if (TARGET MakeASoundAU)
    list(APPEND bundle_id_formats AU)
endif ()

if (TARGET MakeASoundAAX)
    list(APPEND bundle_id_formats AAX)
endif ()

makeasound_add_plugin(BundleIdOverride
        FORMATS ${bundle_id_formats}
        OUTPUT_NAME BundleIdOverride
        BUNDLE_ID com.makeasound.tests.bundleid
        VST3_BUNDLE_ID com.tamberplug.BundleIdOverride.vst3plugin
        AU_BUNDLE_ID com.tamberplug.BundleIdOverride.au
        AUV3_BUNDLE_ID com.makeasound.tests.bundleid.extension
        AAX_BUNDLE_ID com.tamberplug.BundleIdOverride.aax
        BUNDLE_DIRECTORY "${bundle_id_dir}"
        NO_INSTALL
        FOLDER Tests/BundleIds
        SOURCES Fixture.cpp)

makeasound_add_plugin(BundleIdDefault
        FORMATS ${bundle_id_formats}
        OUTPUT_NAME BundleIdDefault
        BUNDLE_DIRECTORY "${bundle_id_dir}"
        NO_INSTALL
        FOLDER Tests/BundleIds
        SOURCES Fixture.cpp)

set(plugin_cmake "${PROJECT_SOURCE_DIR}/CMake/MakeASoundPlugin.cmake")

add_test(NAME CMake/BundleIds/resolve
        COMMAND ${CMAKE_COMMAND} -D "PLUGIN_CMAKE=${plugin_cmake}" -D CASE=resolve
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Resolve.cmake")

add_test(NAME CMake/BundleIds/auv3ExtendsTheApp
        COMMAND ${CMAKE_COMMAND} -D "PLUGIN_CMAKE=${plugin_cmake}" -D CASE=foreignAUv3
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Resolve.cmake")
set_tests_properties(CMake/BundleIds/auv3ExtendsTheApp PROPERTIES
        # CMake wraps a long message, so any space may have become a newline.
        PASS_REGULAR_EXPRESSION
        "does[ \n]+not[ \n]+begin[ \n]+with[ \n]+'com\\.makeasound\\.tests\\.bundleid\\.'")

add_test(NAME CMake/BundleIds/carriedAUv3
        COMMAND ${CMAKE_COMMAND} -D "PLUGIN_CMAKE=${plugin_cmake}" -D CASE=carriedAUv3
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Resolve.cmake")

add_test(NAME CMake/BundleIds/carriedAUv3ExtendsItsApp
        COMMAND ${CMAKE_COMMAND} -D "PLUGIN_CMAKE=${plugin_cmake}"
                -D CASE=foreignCarriedAUv3 -P "${CMAKE_CURRENT_SOURCE_DIR}/Resolve.cmake")
set_tests_properties(CMake/BundleIds/carriedAUv3ExtendsItsApp PROPERTIES
        PASS_REGULAR_EXPRESSION
        "does[ \n]+not[ \n]+begin[ \n]+with[ \n]+'com\\.makeasound\\.tests\\.app\\.'")

if (APPLE AND NOT IOS)
    set(bundle_id_targets "")

    foreach (format IN LISTS bundle_id_formats)
        list(APPEND bundle_id_targets BundleIdOverride-${format} BundleIdDefault-${format})
    endforeach ()

    # The bundles must exist before the test reads them, so they build with the
    # suite, as the hosting fixtures do.
    add_custom_target(BundleIdFixtures DEPENDS ${bundle_id_targets})
    set_target_properties(BundleIdFixtures PROPERTIES FOLDER Tests/BundleIds)
    set(MAKEASOUND_BUNDLE_ID_FIXTURES BundleIdFixtures PARENT_SCOPE)

    # Commas, since a semicolon would split the argument.
    string(REPLACE ";" "," formats_arg "${bundle_id_formats}")

    add_test(NAME CMake/BundleIds/builtBundles
            COMMAND ${CMAKE_COMMAND} -D "DIR=${bundle_id_dir}"
                    -D "FORMATS=${formats_arg}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckBundles.cmake")
endif ()
