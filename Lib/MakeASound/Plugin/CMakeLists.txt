set(CMAKE_FOLDER Lib/Plugin)

# The plugin core links eacp-core; the demo apps find eacp again, which CPM dedupes.
find_package(eacp REQUIRED)

# MakeASoundPlugin: the SDK-free plugin core; format adapters add their SDK on top.
# eacp-core backs its message-thread marshalling, kept out of every public header.
add_library(MakeASoundPlugin STATIC
        Core/Description.cpp
        Core/Plugin.cpp
        Host/PluginWrapper.cpp
        Parameters/Parameter.cpp
        Parameters/FloatParam.cpp
        Parameters/ChoiceParam.cpp
        Parameters/ParameterGroup.cpp
        Parameters/ParameterList.cpp
        Realtime/MessageThread.cpp
        State/StateContext.cpp)

# The message loop a host's run loop can pump is a Linux affair; the rest answer
# that there is none.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(MakeASoundPlugin PRIVATE Realtime/MessageThread-Linux.cpp)
else ()
    target_sources(MakeASoundPlugin PRIVATE Realtime/MessageThread-Default.cpp)
endif ()

target_link_libraries(MakeASoundPlugin PUBLIC MakeASound PRIVATE eacp-core)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundPlugin PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_warnings(MakeASoundPlugin)

# The two drawing tiers, each gated on the eacp target it needs rather than on a
# flag of ours: a plugin that wants eacp-ui for its editor links MakeASoundPluginUI;
# only a <Name>-Standalone executable ever links MakeASoundStandalone.
add_subdirectory(UI)
add_subdirectory(Standalone)
add_subdirectory(VST3)
add_subdirectory(Validation)

# makeasound_add_plugin(), for this tree's Plugins/ and for a CPM consumer's own.
include(MakeASoundPlugin)
