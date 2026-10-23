set(CMAKE_FOLDER Lib/Plugin)

# The plugin core links eacp-core; the demo apps find eacp again, which CPM dedupes.
find_package(eacp REQUIRED)

# MakeASoundPlugin: the SDK-free plugin core; format adapters add their SDK on top.
# eacp-core backs its message-thread marshalling and text formatting, kept out of
# every public header.
add_library(MakeASoundPlugin STATIC
        Compat/ApvtsXml.cpp
        Compat/JuceIdentity.cpp
        Compat/JuceState.cpp
        Compat/JuceTree.cpp
        Core/BypassDelay.cpp
        Core/Description.cpp
        Core/Editor.cpp
        Core/Identity.cpp
        Core/Plugin.cpp
        Host/PluginWrapper.cpp
        Parameters/Parameter.cpp
        Parameters/FloatParam.cpp
        Parameters/IdPath.cpp
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

# hostProcessName(): the executable's name, however the platform tells it.
if (APPLE)
    target_sources(MakeASoundPlugin PRIVATE Core/HostProcess-Apple.cpp)
elseif (WIN32)
    target_sources(MakeASoundPlugin PRIVATE Core/HostProcess-Windows.cpp)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(MakeASoundPlugin PRIVATE Core/HostProcess-Linux.cpp)
else ()
    target_sources(MakeASoundPlugin PRIVATE Core/HostProcess-Default.cpp)
endif ()

# The core, not the device library: a plugin's host is its audio and MIDI. Only
# the standalone format links MakeASound.
target_link_libraries(MakeASoundPlugin PUBLIC MakeASoundCore PRIVATE eacp-core)

if (MAKEASOUND_UNITY_BUILD)
    set_target_properties(MakeASoundPlugin PROPERTIES UNITY_BUILD ON)
endif ()

set_makeasound_target_settings(MakeASoundPlugin)

# The two drawing tiers, each gated on the eacp target it needs rather than on a
# flag of ours: a plugin that wants eacp-ui for its editor links MakeASoundPluginUI;
# only a <Name>-Standalone executable ever links MakeASoundStandalone.
# The host sub-build of an iOS tree needs only MakeASoundAUDescribe, in AU/: no UI
# tier.
if (MAKEASOUND_DESCRIBE_BUILD)
    add_subdirectory(AU)
else ()
    add_subdirectory(UI)
    add_subdirectory(Standalone)
    add_subdirectory(VST3)
    add_subdirectory(AU)
    add_subdirectory(AUv3)
    add_subdirectory(AAX)
    add_subdirectory(Validation)
endif ()

# makeasound_add_plugin(), for this tree's Plugins/ and for a CPM consumer's own,
# and makeasound_add_allocation_tests(), which runs one of them through the
# per-format allocation suites.
include(MakeASoundPlugin)
include(MakeASoundAllocationTests)
