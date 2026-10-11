#pragma once

#include <AudioToolbox/AudioToolbox.h>

namespace MakeASound::AU
{

// Global scope, read-only, the size of a pointer: the unit's Adapter*, for the
// view factory, which the host hands only the AudioUnit. Safe because the view
// class is named per binary, so it only ever asks a unit of its own module.
constexpr AudioUnitPropertyID adapterProperty = 64000;

// The class and bundle kAudioUnitProperty_CocoaUI names, as +1 references the
// host releases; two nulls where the module has no view, and the unit then does
// not answer the property. Defined by whichever of CocoaUI.mm and NoCocoaUI.cpp
// the module compiles.
struct CocoaViewInfo
{
    CFStringRef className = nullptr;
    CFURLRef bundleURL = nullptr;
};

CocoaViewInfo cocoaViewInfo();

} // namespace MakeASound::AU
