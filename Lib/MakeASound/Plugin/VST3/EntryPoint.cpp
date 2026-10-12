// Compiled into each <Name>-VST3 module by makeasound_add_plugin, never into the
// static target: the plugin's own TU defines describeModule().
#include "VST3Common.h"
#include "Factory.h"
#include "../Core/Description.h"
#include "../Realtime/MessageThread.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/main/moduleinit.h"

// Runs from ExitDll/bundleExit/ModuleExit, which every host calls before it
// unloads the module: the message thread's window, scheduler and queue go while
// our code is still mapped. The static destructors that follow at unload run
// under the loader lock, where a thread join would deadlock, so it cannot wait
// for them.
static Steinberg::ModuleTerminator releaseMessageThread(
    [] { MakeASound::releaseHostMessageThread(); });

extern "C" SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API
    GetPluginFactory()
{
    static const auto module = MakeASound::describeModule();

    if (Steinberg::gPluginFactory == nullptr)
        Steinberg::gPluginFactory = MakeASound::VST3::makeFactory(module);
    else
        Steinberg::gPluginFactory->addRef();

    return Steinberg::gPluginFactory;
}
