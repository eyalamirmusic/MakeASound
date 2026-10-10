#include "VST3Common.h"
#include "Factory.h"
#include "Adapter.h"
#include "HostRunLoop.h"
#include "../Realtime/MessageThread.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"

#include <string>

namespace MakeASound::VST3
{

namespace
{
FUnknown* createAdapter(void* context)
{
    adoptHostMessageThread();

    try
    {
        const auto* description = static_cast<const PluginDescription*>(context);
        auto plugin = description->create();

        if (plugin == nullptr)
            return nullptr;

        return static_cast<Vst::IAudioProcessor*>(new Adapter(std::move(plugin)));
    }
    catch (...)
    {
        return nullptr;
    }
}

// The module-wide pump, for deferred work while no view is open.
void onHostContext(FUnknown* context)
{
    static auto loop = OwningPointer<HostRunLoop>();
    loop = attachHostRunLoop(context);
}
} // namespace

Steinberg::FUID classIdFor(const ModuleDescription& module,
                           const PluginDescription& plugin)
{
    return Steinberg::FUID(module.manufacturerCode.toUint32(),
                           plugin.pluginCode.toUint32(),
                           FourCC("VST3").toUint32(),
                           0);
}

Steinberg::CPluginFactory* makeFactory(const ModuleDescription& module)
{
    auto vendor = std::string(module.vendor);

    auto* factory = new Steinberg::CPluginFactory(
        Steinberg::PFactoryInfo(vendor.c_str(),
                                std::string(module.url).c_str(),
                                std::string(module.email).c_str(),
                                Vst::kDefaultFactoryFlags));

    for (const auto& description: module.plugins)
    {
        auto subcategory = description.subcategory.empty()
                               ? defaultSubcategory(description.category)
                               : description.subcategory;

        auto info = Steinberg::PClassInfo2(classIdFor(module, description).toTUID(),
                                           Steinberg::PClassInfo::kManyInstances,
                                           kVstAudioEffectClass,
                                           std::string(description.name).c_str(),
                                           0,
                                           std::string(subcategory).c_str(),
                                           vendor.c_str(),
                                           std::string(description.version).c_str(),
                                           kVstVersionString);

        factory->registerClass(
            &info, &createAdapter, const_cast<PluginDescription*>(&description));
    }

    factory->addHostContextCallback(&onHostContext);

    return factory;
}

} // namespace MakeASound::VST3
