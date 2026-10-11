// Compiled into each <Name>-AU module by makeasound_add_plugin, never into the
// static target: the plugin's own TU defines describeModule().
#include "Adapter.h"
#include "Plist.h"
#include "../Realtime/MessageThread.h"

#include <AudioUnitSDK/AUPlugInDispatch.h>

// The one factory every AudioComponents entry names: the type picks the dispatch
// table, and an unknown subtype or manufacturer instantiates nothing.
extern "C" __attribute__((visibility("default"))) void*
    MakeASoundAUFactory(const AudioComponentDescription* desc);

// What MakeASoundAUPlistGen calls after the link, so the plist comes from this
// module's own describeModule(). 0 when written.
extern "C" __attribute__((visibility("default"))) int
    MakeASoundAUWritePlist(const char* bundleName,
                           const char* bundleId,
                           const char* executable,
                           const char* outputPath);

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

extern "C" int MakeASoundAUWritePlist(const char* bundleName,
                                      const char* bundleId,
                                      const char* executable,
                                      const char* outputPath)
{
    if (bundleName == nullptr || bundleId == nullptr || executable == nullptr
        || outputPath == nullptr)
        return 1;

    try
    {
        using namespace MakeASound;

        auto written = AU::writeAudioComponentsPlist(
            describeModule(), bundleName, bundleId, executable, outputPath);

        return written ? 0 : 1;
    }
    catch (...)
    {
        return 1;
    }
}
