# cmake -D PLUGIN_CMAKE=<CMake/MakeASoundPlugin.cmake> -D CASE=<case> -P Resolve.cmake
#
# makeasound_add_plugin's bundle identifiers as _makeasound_bundle_ids resolves
# them, with no target made:
#
#   resolve      the defaults, BUNDLE_ID's suffixes, and each override replacing
#                only its own format's
#   foreignAUv3  an AUV3_BUNDLE_ID that does not extend BUNDLE_ID: a FATAL_ERROR,
#                which the test expects
include("${PLUGIN_CMAKE}")

function(expect variable expected)
    if (NOT "${${variable}}" STREQUAL "${expected}")
        message(SEND_ERROR "${variable} is '${${variable}}', expected '${expected}'")
    endif ()
endfunction()

macro(clear_ids)
    foreach (variable BUNDLE_ID VST3_BUNDLE_ID AU_BUNDLE_ID AUV3_BUNDLE_ID AAX_BUNDLE_ID)
        unset(ARG_${variable})
    endforeach ()
endmacro()

if (CASE STREQUAL "resolve")
    # Nothing given: the name, lowercased, and one suffix per format.
    clear_ids()
    _makeasound_bundle_ids(MyPlugin)
    expect(ARG_BUNDLE_ID "com.makeasound.myplugin")
    expect(ARG_VST3_BUNDLE_ID "com.makeasound.myplugin.vst3")
    expect(ARG_AU_BUNDLE_ID "com.makeasound.myplugin.component")
    expect(ARG_AUV3_BUNDLE_ID "com.makeasound.myplugin.auv3")
    expect(ARG_AAX_BUNDLE_ID "com.makeasound.myplugin.aaxplugin")

    # BUNDLE_ID alone: its case kept, the suffixes appended.
    clear_ids()
    set(ARG_BUNDLE_ID "com.vendor.MyPlugin")
    _makeasound_bundle_ids(MyPlugin)
    expect(ARG_BUNDLE_ID "com.vendor.MyPlugin")
    expect(ARG_VST3_BUNDLE_ID "com.vendor.MyPlugin.vst3")
    expect(ARG_AU_BUNDLE_ID "com.vendor.MyPlugin.component")
    expect(ARG_AUV3_BUNDLE_ID "com.vendor.MyPlugin.auv3")
    expect(ARG_AAX_BUNDLE_ID "com.vendor.MyPlugin.aaxplugin")

    # One override replaces its own format's whole identifier and no other.
    clear_ids()
    set(ARG_BUNDLE_ID "com.tamberplug.Gain")
    set(ARG_AU_BUNDLE_ID "com.tamberplug.Gain.au")
    _makeasound_bundle_ids(Gain)
    expect(ARG_BUNDLE_ID "com.tamberplug.Gain")
    expect(ARG_VST3_BUNDLE_ID "com.tamberplug.Gain.vst3")
    expect(ARG_AU_BUNDLE_ID "com.tamberplug.Gain.au")
    expect(ARG_AUV3_BUNDLE_ID "com.tamberplug.Gain.auv3")
    expect(ARG_AAX_BUNDLE_ID "com.tamberplug.Gain.aaxplugin")

    # Every one replaced, the AUv3's still inside the app's.
    clear_ids()
    set(ARG_BUNDLE_ID "com.vendor.app")
    set(ARG_VST3_BUNDLE_ID "com.other.v")
    set(ARG_AU_BUNDLE_ID "com.other.a")
    set(ARG_AUV3_BUNDLE_ID "com.vendor.app.ext")
    set(ARG_AAX_BUNDLE_ID "com.other.x")
    _makeasound_bundle_ids(App)
    expect(ARG_VST3_BUNDLE_ID "com.other.v")
    expect(ARG_AU_BUNDLE_ID "com.other.a")
    expect(ARG_AUV3_BUNDLE_ID "com.vendor.app.ext")
    expect(ARG_AAX_BUNDLE_ID "com.other.x")
elseif (CASE STREQUAL "foreignAUv3")
    clear_ids()
    set(ARG_BUNDLE_ID "com.makeasound.tests.bundleid")
    set(ARG_AUV3_BUNDLE_ID "com.makeasound.tests.bundleidextension")
    _makeasound_bundle_ids(BundleId)
    message(STATUS "no error for an AUv3 identifier outside the app's")
else ()
    message(FATAL_ERROR "unknown CASE '${CASE}'")
endif ()
