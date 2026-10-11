// Compiled into each <Name>-AU module by makeasound_add_plugin, never into the
// static target: the plugin's own TU defines describeModule().
#include "Adapter.h"
#include "../Realtime/MessageThread.h"

#include <AudioUnitSDK/AUPlugInDispatch.h>

// The one factory every AudioComponents entry names: the type picks the dispatch
// table, and an unknown subtype or manufacturer instantiates nothing.
extern "C" __attribute__((visibility("default"))) void*
    MakeASoundAUFactory(const AudioComponentDescription* desc);

extern "C" void* MakeASoundAUFactory(const AudioComponentDescription* desc)
{
    using MakeASound::AU::Adapter;

    MakeASound::adoptHostMessageThread();

    if (desc == nullptr || desc->componentManufacturer != Adapter::manufacturer()
        || Adapter::describe(desc->componentSubType) == nullptr)
        return nullptr;

    switch (desc->componentType)
    {
        case kAudioUnitType_MusicDevice:
            return ausdk::AUMusicDeviceFactory<Adapter>::Factory(desc);

        case kAudioUnitType_MusicEffect:
        case kAudioUnitType_MIDIProcessor:
            return ausdk::AUMIDIEffectFactory<Adapter>::Factory(desc);

        case kAudioUnitType_Effect:
            return ausdk::AUBaseFactory<Adapter>::Factory(desc);

        default:
            return nullptr;
    }
}
