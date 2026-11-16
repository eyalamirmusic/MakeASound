// The CLAP adapter over every class of this binary's plugin, as its module's
// factory makes them: CLAPAllocationDriver.h's paths, with the checks any plugin
// allows.

#include "../CLAPAllocationDriver.h"
#include "Classes.h"

#include <NanoTest/NanoTest.h>

namespace PluginAllocationsCLAP
{
using namespace nano;
using namespace CLAPAllocation;
using PluginAllocations::forEachClass;

namespace
{
auto tProcess = test("CLAP/processIsOffTheHeap") = []
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

auto tFlush = test("CLAP/flushIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = hostFor(plugin);
            check(flushAllocations(host) == 0, what);
        });
};

auto tPluginSideChanges = test("CLAP/pluginSideChangesAreOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = hostFor(plugin);
            check(pluginSideChangesAllocations(host, what) == 0, what);
        });
};

auto tHeldPath = test("CLAP/heldParameterPathIsOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = hostFor(plugin);
            check(heldPathAllocations(host) == 0, what);
        });
};

auto tIdleEdits = test("CLAP/idleEditorEditsAreOffTheHeap") = []
{
    forEachClass(
        [](const auto& plugin, std::string_view what)
        {
            auto host = hostFor(plugin);
            check(idleEditAllocations(host) == 0, what);
        });
};
} // namespace
} // namespace PluginAllocationsCLAP
