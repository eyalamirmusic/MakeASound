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

# fdebug.cpp and fdynlib.cpp include <windows.h> bare, and in a unity chunk their
# min/max macros then reach fstring.cpp's std::numeric_limits<int64>::max ().
if (WIN32)
    target_compile_definitions(vst3sdk PRIVATE NOMINMAX)
endif ()

# Steinberg's code, Steinberg's warnings. Always a unity build: nothing here is
# ever edited, so the whole-archive rebuild unity costs an edit never happens.
if (MSVC)
    target_compile_options(vst3sdk PRIVATE /w)
else ()
    target_compile_options(vst3sdk PRIVATE -w)
endif ()

set_target_properties(vst3sdk PROPERTIES UNITY_BUILD ON)

# Two files stay objects of their own: moduleinit.cpp references the moduleHandle
# the platform entry file defines, so only a plugin module may pull it in, and
# vstsinglecomponenteffect.cpp must include its own header before any other has
# included ivsteditcontroller.h, because that header renames two methods on the way.
set_source_files_properties(
        ${VST3_SDK_DIR}/public.sdk/source/main/moduleinit.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

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

# vst3sdk-hosting: the SDK's own IParameterChanges and IEventList, for the tests
# that host the VST3 adapter in-process. Test-only; no plugin links it.
add_library(vst3sdk-hosting STATIC
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/eventlist.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp)

target_link_libraries(vst3sdk-hosting PUBLIC vst3sdk)
set_target_properties(vst3sdk-hosting PROPERTIES UNITY_BUILD ON)

if (MSVC)
    target_compile_options(vst3sdk-hosting PRIVATE /w)
else ()
    target_compile_options(vst3sdk-hosting PRIVATE -w)
endif ()

# ausdk: Apple's AudioUnitSDK (Apache-2.0), fetched rather than vendored, for the
# AU format. Its headers include <expected>, so whatever includes them is C++23.
if (APPLE AND NOT IOS)
    include(CPM)

    CPMAddPackage(
            NAME AudioUnitSDK
            GITHUB_REPOSITORY apple/AudioUnitSDK
            GIT_TAG AudioUnitSDK-1.4.0
            DOWNLOAD_ONLY YES)

    set(AUSDK_DIR "${AudioUnitSDK_SOURCE_DIR}/src/AudioUnitSDK")

    add_library(ausdk STATIC
            ${AUSDK_DIR}/AUBase.cpp
            ${AUSDK_DIR}/AUBuffer.cpp
            ${AUSDK_DIR}/AUBufferAllocator.cpp
            ${AUSDK_DIR}/AUEffectBase.cpp
            ${AUSDK_DIR}/AUInputElement.cpp
            ${AUSDK_DIR}/AUMIDIBase.cpp
            ${AUSDK_DIR}/AUMIDIEffectBase.cpp
            ${AUSDK_DIR}/AUOutputElement.cpp
            ${AUSDK_DIR}/AUPlugInDispatch.cpp
            ${AUSDK_DIR}/AUScopeElement.cpp
            ${AUSDK_DIR}/ComponentBase.cpp
            ${AUSDK_DIR}/MusicDeviceBase.cpp)

    target_include_directories(ausdk SYSTEM PUBLIC "${AudioUnitSDK_SOURCE_DIR}/include")
    target_compile_features(ausdk PUBLIC cxx_std_23)
    target_compile_options(ausdk PRIVATE -w)
    target_link_libraries(ausdk PUBLIC
            "-framework AudioToolbox"
            "-framework CoreAudio"
            "-framework CoreMIDI"
            "-framework CoreFoundation")

    set_target_properties(ausdk PROPERTIES
            FOLDER External/AudioUnitSDK
            UNITY_BUILD ON)
endif ()
