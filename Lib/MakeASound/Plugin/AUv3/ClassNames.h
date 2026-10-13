#pragma once

// The AUv3 format's Objective-C class names, which carry a suffix of the
// module's: the runtime is one per process, and two of our extensions loaded into
// one host would otherwise share whichever class registered first. The sources on
// MakeASoundAUv3's MAKEASOUND_AUV3_SOURCES are compiled into each module with
// MAKEASOUND_AUV3_CLASS_SUFFIX defined, makeasound_add_plugin deriving it from the
// bundle id and version as stage 5 does the AU view's; the tests define their own.
// The static library itself defines no class.

#ifndef MAKEASOUND_AUV3_CLASS_SUFFIX
#error "MAKEASOUND_AUV3_CLASS_SUFFIX must name the module, e.g. MakeASoundTests"
#endif

#define MAKEASOUND_AUV3_JOIN3(a, b, c) a##b##c
#define MAKEASOUND_AUV3_JOIN(a, b, c) MAKEASOUND_AUV3_JOIN3(a, b, c)

// MAKEASOUND_AUV3_CLASS(MakeASoundAudioUnit) is MakeASoundAudioUnit_<suffix>.
#define MAKEASOUND_AUV3_CLASS(name)                                                 \
    MAKEASOUND_AUV3_JOIN(name, _, MAKEASOUND_AUV3_CLASS_SUFFIX)

#define MAKEASOUND_AUV3_STRING2(name) #name
#define MAKEASOUND_AUV3_STRING(name) MAKEASOUND_AUV3_STRING2(name)

// The class's name as a C string, for a plist's NSExtensionPrincipalClass.
#define MAKEASOUND_AUV3_CLASS_NAME(name)                                            \
    MAKEASOUND_AUV3_STRING(MAKEASOUND_AUV3_CLASS(name))
