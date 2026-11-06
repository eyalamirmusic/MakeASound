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

# vst3sdk-hosting: the host side of the SDK. The module loader (one platform file
# each), the host classes a plugin is initialised with, and the SDK's own
# IParameterChanges and IEventList. MakeASoundHosting links it, and so do the tests
# that host the VST3 adapter in-process; no plugin does.
add_library(vst3sdk-hosting STATIC
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/eventlist.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp)

# The loader that differs per platform: Cocoa's NSBundle under ARC, LoadLibraryW
# (and Shell32's known folders, which the file links itself through a pragma), or
# dlopen. iOS has none: hosting is macOS and Windows for now (Linux next), and the
# Mac loader is Cocoa's, so an iOS tree keeps the platform-neutral host side the
# VST3 test host uses, and Module::create is not defined there.
if (IOS)
    set(vst3_module_loader)
elseif (APPLE)
    set(vst3_module_loader ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
    set_source_files_properties(${vst3_module_loader} PROPERTIES
            COMPILE_OPTIONS -fobjc-arc
            SKIP_UNITY_BUILD_INCLUSION ON)
    target_link_libraries(vst3sdk-hosting PUBLIC "-framework Cocoa")
elseif (WIN32)
    set(vst3_module_loader ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
    target_compile_definitions(vst3sdk-hosting PRIVATE NOMINMAX)
    # The loader picks its C++20 path (u8string to std::string) by SMTG_CPP20,
    # which reads __cplusplus, and MSVC reports 199711L without this switch, so
    # it took the pre-C++20 path that passes a std::u8string as a std::string.
    # clang-cl already reports the real value.
    target_compile_options(vst3sdk-hosting PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>)
else ()
    set(vst3_module_loader ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_linux.cpp)
endif ()

if (vst3_module_loader)
    target_sources(vst3sdk-hosting PRIVATE ${vst3_module_loader})
endif ()

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
            DOWNLOAD_ONLY YES
            QUIET)

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

# aaxsdk: Avid's AAX SDK 2.8.1 from the audiosdk/aax fork, fetched only with
# MAKEASOUND_BUILD_AAX, so a default build compiles no Avid code. Dual-licensed:
# Avid's AAX SDK License Agreement or GPLv3. Both ACF glue files belong to each
# module, so they stay out of the archive: AAX_Init.cpp is included by the module's
# EntryPoint.cpp, its shutdown renamed, and AAX_Exports.cpp is compiled beside it.
# Built on macOS and on Windows x64 only: the SDK's ACF/acfbasetypes.h knows no
# Windows ARM64 (#error Unknown platform without _M_AMD64), and AAX.h's
# pointer-size check wants _M_X64, so an ARM64 Windows build skips the format.
set(aax_sdk_platform OFF)

if (MAKEASOUND_BUILD_AAX AND APPLE AND NOT IOS)
    set(aax_sdk_platform ON)
elseif (MAKEASOUND_BUILD_AAX AND WIN32)
    set(aax_arch_id "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")

    if (NOT aax_arch_id)
        set(aax_arch_id "${CMAKE_SYSTEM_PROCESSOR}")
    endif ()

    string(TOUPPER "${aax_arch_id}" aax_arch_id)

    if (aax_arch_id MATCHES "^(X64|AMD64|X86_64)$")
        set(aax_sdk_platform ON)
    else ()
        message(STATUS "aaxsdk skipped (the AAX SDK builds for Windows x64 only, "
                "not '${aax_arch_id}')")
    endif ()
endif ()

if (aax_sdk_platform)
    include(CPM)

    CPMAddPackage(
            NAME AAXSDK
            VERSION 2.8.1
            GITHUB_REPOSITORY audiosdk/aax
            GIT_TAG faf5d8e2c339b1c6053d86501465e5679b16314e
            DOWNLOAD_ONLY YES
            QUIET)

    set(AAX_SDK_DIR "${AAXSDK_SOURCE_DIR}")

    if (NOT EXISTS "${AAX_SDK_DIR}/Interfaces/AAX.h")
        message(FATAL_ERROR "aaxsdk: no Interfaces/AAX.h under '${AAX_SDK_DIR}'; "
                "CPM_AAXSDK_SOURCE must name the root of an AAX SDK")
    endif ()

    message(STATUS "aaxsdk: the AAX SDK is fetched from the audiosdk/aax fork. "
            "An AAX binary built with it is GPLv3 unless its author holds "
            "Avid's AAX SDK License Agreement.")

    set(AAX_LIB_DIR "${AAX_SDK_DIR}/Libs/AAXLibrary/source")

    # The glob takes the Windows autorelease pool too, which the Mac build swaps
    # for the Objective-C one.
    file(GLOB aax_library_sources CONFIGURE_DEPENDS "${AAX_LIB_DIR}/*.cpp")
    list(REMOVE_ITEM aax_library_sources "${AAX_LIB_DIR}/AAX_Init.cpp")

    if (APPLE)
        list(REMOVE_ITEM aax_library_sources
                "${AAX_LIB_DIR}/AAX_CAutoreleasePool.Win.cpp")
        list(APPEND aax_library_sources "${AAX_LIB_DIR}/AAX_CAutoreleasePool.OSX.mm")
    endif ()

    add_library(aaxsdk STATIC
            ${aax_library_sources}
            ${AAX_SDK_DIR}/Interfaces/ACF/CACFClassFactory.cpp)

    target_include_directories(aaxsdk SYSTEM PUBLIC
            "${AAX_SDK_DIR}/Interfaces"
            "${AAX_SDK_DIR}/Interfaces/ACF"
            "${AAX_SDK_DIR}/Libs"
            "${AAX_SDK_DIR}/Libs/AAXLibrary/include"
            "${AAX_SDK_DIR}/Extensions")

    target_compile_features(aaxsdk PUBLIC cxx_std_20)
    target_compile_options(aaxsdk PRIVATE -w)

    # AAX_CMutex.cpp is the one TU that includes <windows.h>, and the unity build
    # hands its macros to every file after it. The vcxproj's other definitions
    # (_WINDOWS, I18N_LIB, PLUGIN_SDK_BUILD, AAX_PLUGIN_BUILD) are read by no
    # source, and the two _NO_DEPRECATE ones only quiet warnings -w already drops.
    if (APPLE)
        target_link_libraries(aaxsdk PUBLIC
                "-framework Foundation"
                "-framework CoreFoundation")
    else ()
        target_compile_definitions(aaxsdk PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif ()

    # AAX_CUIDs.cpp defines INITACFIDS before the headers that declare every uid,
    # so it must be the first to include them: an object of its own.
    set_source_files_properties("${AAX_LIB_DIR}/AAX_CUIDs.cpp"
            PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

    set_target_properties(aaxsdk PROPERTIES
            FOLDER External/AAX_SDK
            UNITY_BUILD ON
            MAKEASOUND_AAX_SDK_EXPORTS "${AAX_SDK_DIR}/Interfaces/AAX_Exports.cpp")
endif ()
