# makeasound_default_setup()
#
# What MakeASound configures itself with when it is the top-level project. A
# project that fetches MakeASound gets the same by calling it from its own root
# once the fetch has defined it. Each setting yields to a value already given, so
# a -D on the command line wins.
#
#   The deployment floors: macOS 11.0 and iOS 15.0, eacp's.
macro(makeasound_default_setup)
    # CMake's Darwin module creates CMAKE_OSX_DEPLOYMENT_TARGET as an empty cache
    # entry in project(), so a plain cache set is a no-op and every binary would
    # target the build machine's SDK. Claim the entry only while it is empty.
    if (APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        if (IOS)
            set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
                    "Minimum iOS version" FORCE)
        else ()
            set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
                    "Minimum macOS version" FORCE)
        endif ()
    endif ()
endmacro()
