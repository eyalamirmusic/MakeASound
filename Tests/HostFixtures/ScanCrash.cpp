// HostFixtureScanCrash: aborts the moment the factory creates it, the way a
// plugin that crashes on load takes its host down. Scanned only out of process.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <cstdlib>

namespace MakeASound
{

ModuleDescription describeModule()
{
    auto module = ModuleDescription {};
    module.vendor = "MakeASound Tests";
    module.manufacturerCode = "MkTs";
    module.plugins.add({.name = "HostFixtureScanCrash",
                        .category = Category::Effect,
                        .pluginCode = "HfCr",
                        .create = []() -> OwningPointer<Plugin> { std::abort(); }});
    return module;
}

} // namespace MakeASound
