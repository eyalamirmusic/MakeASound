// This binary's plugin's Audio Units, registered in process with the module's
// own factory and loaded through MakeASoundHosting as a host loads them:
// HostingAllocationDriver.h's busy blocks over every class, the host side
// measured with it.

#include "../HostingAllocationDriver.h"
#include "../AUTestHost.h"
#include "Classes.h"

#include <MakeASound/Hosting/AU/HostedAudioUnit.h>

#include <NanoTest/NanoTest.h>

namespace PluginAllocationsHostedAU
{
using namespace nano;
using namespace HostingAllocation;
using PluginAllocations::forEachClass;

namespace
{
auto tBlocks = test("Hosting/hostedAUBlocksAreOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto component =
                AUHost::registerComponent(AUHost::descriptionFor(plugin.pluginCode));
            auto loaded = MakeASound::Hosting::loadAudioUnit(component, {}, {});
            check(loaded.plugin != nullptr, what);

            if (loaded.plugin != nullptr)
                check(busyBlocksAllocations(*loaded.plugin, TestPlugins::blockSize)
                          == 0,
                      what);
        });
};
} // namespace
} // namespace PluginAllocationsHostedAU
