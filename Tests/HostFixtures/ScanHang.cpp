// HostFixtureScanHang: sleeps for a minute when the factory creates it, longer
// than any scan timeout the suites set, so the worker is killed. Scanned only out
// of process.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <chrono>
#include <thread>

namespace MakeASound
{

ModuleDescription describeModule()
{
    auto module = ModuleDescription {};
    module.vendor = "MakeASound Tests";
    module.manufacturerCode = "MkTs";
    module.plugins.add({.name = "HostFixtureScanHang",
                        .category = Category::Effect,
                        .pluginCode = "HfHg",
                        .create = []() -> OwningPointer<Plugin>
                        {
                            std::this_thread::sleep_for(std::chrono::seconds(60));
                            return {};
                        }});
    return module;
}

} // namespace MakeASound
