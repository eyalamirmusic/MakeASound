#pragma once

// The factory dispatch every AU entry shares: EntryPoint.cpp's over the module's
// own plugins, and a test's over a description of its own.
#include "AUCommon.h"
#include "../Core/Description.h"

#include <AudioUnitSDK/AUPlugInDispatch.h>

namespace MakeASound::AU
{

// The SDK's factory over AdapterType for the requested component type, so each
// type gets its own dispatch table: AUMusicDeviceFactory for 'aumu',
// AUMIDIEffectFactory for 'aumf' and 'aumi', AUBaseFactory for 'aufx' and the
// other MIDI-less types a ported plugin's auType may name. Null for no plugin,
// or for a type other than the plugin's auType when it sets one, which is the
// only type its plist registers.
template <typename AdapterType>
void* dispatchFactory(const AudioComponentDescription* desc,
                      const PluginDescription* plugin)
{
    if (desc == nullptr || plugin == nullptr)
        return nullptr;

    if (plugin->auType && plugin->auType->toUint32() != desc->componentType)
        return nullptr;

    switch (desc->componentType)
    {
        case kAudioUnitType_MusicDevice:
            return ausdk::AUMusicDeviceFactory<AdapterType>::Factory(desc);

        case kAudioUnitType_MusicEffect:
        case kAudioUnitType_MIDIProcessor:
            return ausdk::AUMIDIEffectFactory<AdapterType>::Factory(desc);

        case kAudioUnitType_Effect:
        case kAudioUnitType_Generator:
        case kAudioUnitType_Mixer:
        case kAudioUnitType_Panner:
        case kAudioUnitType_FormatConverter:
            return ausdk::AUBaseFactory<AdapterType>::Factory(desc);

        default:
            return nullptr;
    }
}

} // namespace MakeASound::AU
