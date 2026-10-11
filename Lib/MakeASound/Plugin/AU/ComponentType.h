#pragma once

#include "../Core/Description.h"

#include <AudioToolbox/AudioToolbox.h>

#include <string>
#include <string_view>

namespace MakeASound::AU
{

// What the bundle's plist and the factory say about one plugin class. SDK-free,
// so the plist generator links it without the AudioUnitSDK.
struct ComponentInfo
{
    OSType type = kAudioUnitType_Effect;
    OSType subtype = 0;
    OSType manufacturer = 0;
    int numInputBuses = 0;
    int numOutputBuses = 0;
};

// Constructs the plugin once to read its layout: an instrument is 'aumu', a MIDI
// effect 'aumi', an effect with a MIDI input 'aumf', any other effect 'aufx'.
ComponentInfo componentInfoFor(const ModuleDescription& module,
                               const PluginDescription& plugin);

// Null when no plugin of the module has that subtype.
const PluginDescription* findPlugin(const ModuleDescription& module,
                                    OSType subtype) noexcept;

std::string fourCCString(OSType code);

// "major.minor.patch" as AudioComponents wants it: major << 16 | minor << 8 |
// patch, a missing or unreadable part counting as 0.
UInt32 versionNumber(std::string_view version) noexcept;

} // namespace MakeASound::AU
