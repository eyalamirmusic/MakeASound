// This binary's plugin's own VST3 bundle, scanned and loaded through
// MakeASoundHosting as a host loads it: HostingAllocationDriver.h's busy blocks
// over every class the bundle's factory lists, the host side measured with it.
// MAKEASOUND_ALLOCATION_VST3_BUNDLE is the bundle's path.

#include "../HostingAllocationDriver.h"
#include "Classes.h"

#include <NanoTest/NanoTest.h>

#include <string>

namespace PluginAllocationsHostedVST3
{
using namespace nano;
using namespace MakeASound;
using namespace HostingAllocation;

namespace
{
struct DefaultFormats : Hosting::FormatManager
{
    DefaultFormats() { addDefaultFormats(); }
};

auto tBlocks = test("Hosting/hostedVST3BlocksAreOffTheHeap") = []
{
    const auto formats = DefaultFormats();
    auto scan = formats.scanTarget(MAKEASOUND_ALLOCATION_VST3_BUNDLE).scan;
    check(scan.classes.size() == PluginAllocations::module().plugins.size());

    for (const auto& found: scan.classes)
    {
        auto label = std::string(found.name);
        auto plugin = std::move(formats.load(found).plugin);
        check(plugin != nullptr, label);

        if (plugin != nullptr)
            check(busyBlocksAllocations(*plugin, 128) == 0, label);
    }
};
} // namespace
} // namespace PluginAllocationsHostedVST3
