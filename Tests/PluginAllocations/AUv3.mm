// The AUv3 adapter over every class of this binary's plugin, each a unit of the
// AUAudioUnit subclass compiled in under this binary's class suffix:
// AUv3AllocationDriver.h's paths, with the checks any plugin allows.

#include "../AUv3AllocationDriver.h"
#include "Classes.h"

#include <NanoTest/NanoTest.h>

namespace PluginAllocationsAUv3
{
using namespace nano;
using namespace AUv3Allocation;
using PluginAllocations::forEachClass;

namespace
{
auto tRender = test("AUv3/renderIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = UnitHost<Plugin>(plugin.pluginCode);
            checkRenderIsOffTheHeap(host, what);
        });
};

auto tHeldRender = test("AUv3/heldParameterRenderIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = UnitHost<Plugin>(plugin.pluginCode);
            checkHeldRenderIsOffTheHeap(host, what);
        });
};
} // namespace
} // namespace PluginAllocationsAUv3
