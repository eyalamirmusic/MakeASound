# vst3sdk: the vendored VST 3 SDK in ThirdParty/VST3_SDK (see README.md there),
# built here in place of Steinberg's own CMake. Plugin side only; the VST3 format
# links it, and nothing else does.
set(CMAKE_FOLDER External/VST3_SDK)

set(VST3_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/VST3_SDK")

add_library(vst3sdk STATIC
        ${VST3_SDK_DIR}/base/source/baseiids.cpp
        ${VST3_SDK_DIR}/base/source/fbuffer.cpp
        ${VST3_SDK_DIR}/base/source/fdebug.cpp
        ${VST3_SDK_DIR}/base/source/fdynlib.cpp
        ${VST3_SDK_DIR}/base/source/fobject.cpp
        ${VST3_SDK_DIR}/base/source/fstreamer.cpp
        ${VST3_SDK_DIR}/base/source/fstring.cpp
        ${VST3_SDK_DIR}/base/source/timer.cpp
        ${VST3_SDK_DIR}/base/source/updatehandler.cpp
        ${VST3_SDK_DIR}/base/thread/source/fcondition.cpp
        ${VST3_SDK_DIR}/base/thread/source/flock.cpp
        ${VST3_SDK_DIR}/pluginterfaces/base/conststringtable.cpp
        ${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp
        ${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp
        ${VST3_SDK_DIR}/pluginterfaces/base/ustring.cpp
        ${VST3_SDK_DIR}/public.sdk/source/common/commoniids.cpp
        ${VST3_SDK_DIR}/public.sdk/source/common/commonstringconvert.cpp
        ${VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp
        ${VST3_SDK_DIR}/public.sdk/source/common/pluginview.cpp
        ${VST3_SDK_DIR}/public.sdk/source/main/moduleinit.cpp
        ${VST3_SDK_DIR}/public.sdk/source/main/pluginfactory.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/utility/stringconvert.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstaudioeffect.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstbus.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstcomponent.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstcomponentbase.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vsteditcontroller.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstinitiids.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstnoteexpressiontypes.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstparameters.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstpresetfile.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstrepresentation.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp)

# SYSTEM, so a consumer's warnings stay off Steinberg's headers.
target_include_directories(vst3sdk SYSTEM PUBLIC "${VST3_SDK_DIR}")
target_compile_features(vst3sdk PUBLIC cxx_std_17)

# fdebug.h insists on one of the two; vstsinglecomponenteffect.cpp would otherwise
# include vsteditcontroller.cpp a second time.
target_compile_definitions(vst3sdk
        PUBLIC $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>
        PRIVATE PROJECT_INCLUDES_VSTEDITCONTROLLER=1
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)

# Steinberg's code, Steinberg's warnings.
if (MSVC)
    target_compile_options(vst3sdk PRIVATE /w)
else ()
    target_compile_options(vst3sdk PRIVATE -w)
endif ()

if (APPLE)
    target_link_libraries(vst3sdk PUBLIC "-framework CoreFoundation")
    set(VST3_SDK_MAIN "${VST3_SDK_DIR}/public.sdk/source/main/macmain.cpp")
    set_target_properties(vst3sdk PROPERTIES MAKEASOUND_VST3_SDK_EXPORTS
            "${VST3_SDK_DIR}/public.sdk/source/main/macexport.exp")
elseif (WIN32)
    set(VST3_SDK_MAIN "${VST3_SDK_DIR}/public.sdk/source/main/dllmain.cpp")
else ()
    find_package(Threads REQUIRED)
    target_link_libraries(vst3sdk PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    set(VST3_SDK_MAIN "${VST3_SDK_DIR}/public.sdk/source/main/linuxmain.cpp")
endif ()

set_target_properties(vst3sdk PROPERTIES MAKEASOUND_VST3_SDK_MAIN "${VST3_SDK_MAIN}")
