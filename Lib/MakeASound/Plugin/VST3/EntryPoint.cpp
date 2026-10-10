// Compiled into each <Name>-VST3 module by makeasound_add_plugin, never into the
// static target: the plugin's own TU defines describeModule().
#include "VST3Common.h"
#include "Factory.h"
#include "../Core/Description.h"

#include "public.sdk/source/main/pluginfactory.h"

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
