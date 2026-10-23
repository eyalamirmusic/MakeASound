// The standalone format's block over every class of this binary's plugin, as the
// app wraps it: StandaloneAllocationDriver.h's block on Engine.

#include "../StandaloneAllocationDriver.h"
#include "Classes.h"

#include <NanoTest/NanoTest.h>

namespace PluginAllocationsStandalone
{
using namespace nano;
using PluginAllocations::forEachClass;

namespace
{
auto tBlock = test("Standalone/blockIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        { check(StandaloneAllocation::blockAllocations(plugin) == 0, what); });
};
} // namespace
} // namespace PluginAllocationsStandalone
