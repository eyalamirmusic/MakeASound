# StandaloneLink: an app over MakeASoundStandaloneCore with no describeModule(),
# as an app that builds its own plugin is. A StandaloneHost.cpp that references
# the module again fails this target's link, so the build itself is the test;
# ctest also runs it once.
add_executable(StandaloneLink Main.cpp)
target_link_libraries(StandaloneLink PRIVATE MakeASoundStandaloneCore)
set_makeasound_target_settings(StandaloneLink)
set_target_properties(StandaloneLink PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

add_test(NAME Standalone/linksWithoutADescribeModule COMMAND StandaloneLink)
