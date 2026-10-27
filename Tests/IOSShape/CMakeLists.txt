# makeasound_add_plugin's ORIENTATION and DEVICE_FAMILY, checked at the CMake
# level in script mode (Resolve.cmake), so on every platform:
#
#   CMake/IOSShape/resolve          the defaults, each orientation and each family
#   CMake/IOSShape/badOrientation   an unknown ORIENTATION is an error
#   CMake/IOSShape/badDeviceFamily  an unknown DEVICE_FAMILY is an error
#   CMake/IOSShape/carriedAUv3      an AUv3 another plugin's app carries
#                                   (AUV3_APP) installs on that app's families
#   CMake/IOSShape/carriedAUv3Shape and naming DEVICE_FAMILY there is an error
set(plugin_cmake "${PROJECT_SOURCE_DIR}/CMake/MakeASoundPlugin.cmake")

add_test(NAME CMake/IOSShape/resolve
        COMMAND ${CMAKE_COMMAND} -D "PLUGIN_CMAKE=${plugin_cmake}" -D CASE=resolve
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Resolve.cmake")

add_test(NAME CMake/IOSShape/badOrientation
        COMMAND ${CMAKE_COMMAND} -D "PLUGIN_CMAKE=${plugin_cmake}"
                -D CASE=badOrientation -P "${CMAKE_CURRENT_SOURCE_DIR}/Resolve.cmake")
set_tests_properties(CMake/IOSShape/badOrientation PROPERTIES
        # CMake wraps a long message, so any space may have become a newline.
        PASS_REGULAR_EXPRESSION "portrait[ \n]+or[ \n]+landscape,[ \n]+not[ \n]+'sideways'")

add_test(NAME CMake/IOSShape/badDeviceFamily
        COMMAND ${CMAKE_COMMAND} -D "PLUGIN_CMAKE=${plugin_cmake}"
                -D CASE=badDeviceFamily -P "${CMAKE_CURRENT_SOURCE_DIR}/Resolve.cmake")
set_tests_properties(CMake/IOSShape/badDeviceFamily PROPERTIES
        PASS_REGULAR_EXPRESSION "iphone[ \n]+and[ \n]+ipad,[ \n]+not[ \n]+'watch'")

add_test(NAME CMake/IOSShape/carriedAUv3
        COMMAND ${CMAKE_COMMAND} -D "PLUGIN_CMAKE=${plugin_cmake}" -D CASE=carriedAUv3
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Resolve.cmake")

add_test(NAME CMake/IOSShape/carriedAUv3Shape
        COMMAND ${CMAKE_COMMAND} -D "PLUGIN_CMAKE=${plugin_cmake}"
                -D CASE=carriedAUv3Shape -P "${CMAKE_CURRENT_SOURCE_DIR}/Resolve.cmake")
set_tests_properties(CMake/IOSShape/carriedAUv3Shape PROPERTIES
        PASS_REGULAR_EXPRESSION "belong[ \n]+to[ \n]+the[ \n]+call[ \n]+that[ \n]+makes")
