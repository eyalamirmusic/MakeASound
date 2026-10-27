# cmake -D PLUGIN_CMAKE=<CMake/MakeASoundPlugin.cmake> -D CASE=<case> -P Resolve.cmake
#
# makeasound_add_plugin's ORIENTATION and DEVICE_FAMILY as _makeasound_ios_shape
# resolves them, with no target made:
#
#   resolve           the defaults (no lock, iPhone and iPad), each orientation
#                     and each family
#   badOrientation    an ORIENTATION other than portrait or landscape: a
#                     FATAL_ERROR, which the test expects
#   badDeviceFamily   a DEVICE_FAMILY other than iphone or ipad: the same
include("${PLUGIN_CMAKE}")

function(expect variable expected)
    if (NOT "${${variable}}" STREQUAL "${expected}")
        message(SEND_ERROR "${variable} is '${${variable}}', expected '${expected}'")
    endif ()
endfunction()

macro(clear_shape)
    unset(ARG_ORIENTATION)
    unset(ARG_DEVICE_FAMILY)
endmacro()

if (CASE STREQUAL "resolve")
    # Nothing given: the device rotates the app, which installs everywhere.
    clear_shape()
    _makeasound_ios_shape(App)
    expect(ARG_IOS_ORIENTATIONS "")
    expect(ARG_IOS_DEVICE_FAMILY "1;2")

    clear_shape()
    set(ARG_ORIENTATION portrait)
    _makeasound_ios_shape(App)
    expect(ARG_IOS_ORIENTATIONS "UIInterfaceOrientationPortrait")

    # Landscape is either way up.
    clear_shape()
    set(ARG_ORIENTATION landscape)
    _makeasound_ios_shape(App)
    expect(ARG_IOS_ORIENTATIONS
            "UIInterfaceOrientationLandscapeLeft;UIInterfaceOrientationLandscapeRight")

    clear_shape()
    set(ARG_DEVICE_FAMILY ipad)
    _makeasound_ios_shape(App)
    expect(ARG_IOS_DEVICE_FAMILY "2")

    clear_shape()
    set(ARG_DEVICE_FAMILY iphone)
    _makeasound_ios_shape(App)
    expect(ARG_IOS_DEVICE_FAMILY "1")

    # Apple's order whatever the caller's, and each family once.
    clear_shape()
    set(ARG_DEVICE_FAMILY ipad iphone ipad)
    _makeasound_ios_shape(App)
    expect(ARG_IOS_DEVICE_FAMILY "1;2")
elseif (CASE STREQUAL "badOrientation")
    clear_shape()
    set(ARG_ORIENTATION sideways)
    _makeasound_ios_shape(App)
    message(STATUS "no error for an unknown orientation")
elseif (CASE STREQUAL "badDeviceFamily")
    clear_shape()
    set(ARG_DEVICE_FAMILY ipad watch)
    _makeasound_ios_shape(App)
    message(STATUS "no error for an unknown device family")
else ()
    message(FATAL_ERROR "unknown CASE '${CASE}'")
endif ()
