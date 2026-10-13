set(CMAKE_FOLDER Tools)

if (TARGET MakeASoundPluginval)
    add_subdirectory(PluginValidator)
endif ()

if (IOS)
    add_subdirectory(AUv3Probe)
endif ()
