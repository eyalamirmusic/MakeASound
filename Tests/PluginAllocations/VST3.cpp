// The VST3 adapter over every class of this binary's plugin, as its module's
// factory makes them: VST3AllocationDriver.h's paths, with the checks any plugin
// allows.

#include "../VST3AllocationDriver.h"
#include "Classes.h"

#include <NanoTest/NanoTest.h>

namespace PluginAllocationsVST3
{
using namespace nano;
using namespace VST3Allocation;
using PluginAllocations::forEachClass;

namespace
{
auto tProcess = test("VST3/processIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            for (auto inPlace: {false, true})
            {
                auto host = hostFor(plugin);
                checkProcessIsOffTheHeap(host, inPlace, what);
            }
        });
};

auto tSetParamNormalized = test("VST3/setParamNormalizedIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = hostFor(plugin);
            check(setParamNormalizedAllocations(host) == 0, what);
        });
};

auto tPluginSideChanges = test("VST3/pluginSideChangesAreOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = hostFor(plugin);
            check(pluginSideChangesAllocations(host, what) == 0, what);
        });
};

auto tHeldPath = test("VST3/heldParameterPathIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = hostFor(plugin);
            check(heldPathAllocations(host) == 0, what);
        });
};
} // namespace
} // namespace PluginAllocationsVST3
