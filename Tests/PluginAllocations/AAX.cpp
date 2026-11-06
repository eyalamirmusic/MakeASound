// The AAX adapter over every class of this binary's plugin, each created as Pro
// Tools creates it, by effect id, on the first stem pair it accepts:
// AAXAllocationDriver.h's paths, with the checks any plugin allows.

#include "../AAXAllocationDriver.h"
#include "Classes.h"

#include <NanoTest/NanoTest.h>

// The binary's bundle id, as EntryPoint.cpp defines a module's; every effect id
// hostFor() names derives from it.
std::string_view MakeASound::AAX::moduleBundleId() noexcept
{
    return "com.makeasound.allocationtests";
}

namespace PluginAllocationsAAX
{
using namespace nano;
using namespace AAXAllocation;
using PluginAllocations::forEachClass;

namespace
{
auto tRender = test("AAX/renderIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto stems = firstStemPair(plugin);
            check(stems.has_value(), what);

            if (!stems)
                return;

            for (auto inPlace: {false, true})
            {
                auto host = hostFor(plugin, *stems);

                if (inPlace)
                    host.processInPlace();

                check(allocationsOverBlocks(host) == 0, what);
            }
        });
};

auto tHeldRender = test("AAX/heldParameterRenderIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto stems = firstStemPair(plugin);
            check(stems.has_value(), what);

            if (!stems)
                return;

            auto host = hostFor(plugin, *stems);
            checkHeldRenderIsOffTheHeap(host, what);
        });
};
} // namespace
} // namespace PluginAllocationsAAX
