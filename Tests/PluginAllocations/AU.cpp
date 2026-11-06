// The AU adapter over every class of this binary's plugin, each registered in
// process with the module's own factory and driven through AudioToolbox's C API:
// AUAllocationDriver.h's paths, with the checks any plugin allows.

#include "../AUAllocationDriver.h"
#include "Classes.h"

#include <NanoTest/NanoTest.h>

namespace PluginAllocationsAU
{
using namespace nano;
using namespace AUAllocation;
using PluginAllocations::forEachClass;

namespace
{
auto tRender = test("AU/renderIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = UnitHost<Plugin>(componentFor(plugin.pluginCode));
            checkRenderIsOffTheHeap(host, what);
        });
};

auto tHeldRender = test("AU/heldParameterRenderIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = UnitHost<Plugin>(componentFor(plugin.pluginCode));
            checkHeldRenderIsOffTheHeap(host, what);
        });
};

auto tPluginSideMoves = test("AU/pluginSideMovesAreOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = UnitHost<Plugin>(componentFor(plugin.pluginCode));
            check(pluginSideMovesAllocations(host, what) == 0, what);
        });
};
} // namespace
} // namespace PluginAllocationsAU
