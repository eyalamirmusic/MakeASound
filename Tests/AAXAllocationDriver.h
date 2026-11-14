#pragma once

// The AAX adapter's audio-thread paths under the allocation ban, over any
// plugin of this binary's describeModule(): what AAXAllocationTests.cpp measures
// with its fixtures, and what makeasound_add_allocation_tests measures over
// every class of a consumer's (PluginAllocations/AAX.cpp). Each driver returns
// how many times the heap was reached for; the checks a fixture's members allow
// stay in the fixture suite.

#include "AllocationProbe.h"
#include "AAXTestHost.h"

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace AAXAllocation
{
using namespace AAXHost;
using Probe::allocationsIn;

// The first stem pair the plugin accepts, in the adapter's append-only order, as
// describeEffects() publishes them; none when it accepts none.
inline std::optional<Format::StemPair> firstStemPair(const PluginDescription& plugin)
{
    setCurrentPluginFormat(PluginFormat::AAX);
    auto instance = plugin.create();

    if (instance == nullptr)
        return {};

    instance->setFormat(PluginFormat::AAX);
    auto declared = instance->getBusLayout();

    for (const auto& pair: Format::stemPairs())
        if (auto layout = Format::layoutFor(declared, pair))
            if (instance->acceptsLayout(*layout))
                return pair;

    return {};
}

// An adapter as Pro Tools creates one for `plugin` of this binary's module: by
// its effect id under moduleBundleId(), on its first stem pair.
inline AdapterHost<Plugin> hostFor(const PluginDescription& plugin,
                                   Format::StemPair stems)
{
    return AdapterHost<Plugin>(nullptr,
                               {},
                               aaxEffectIdFor(Format::moduleBundleId(), plugin),
                               stems.input,
                               stems.output);
}

// The registered ids of every exposed parameter, built before the ban: an id
// past the library's short-string capacity would allocate in the test's string.
template <typename P>
std::vector<std::string> exposedIds(AdapterHost<P>& host)
{
    auto ids = std::vector<std::string> {};
    const auto& list = host.plugin().parameters();

    for (auto i = 0; i < list.size(); ++i)
        if (list.isHostExposed(i))
            ids.push_back(host.idOf(list[i]));

    return ids;
}

// Everything a busy block carries: a write to every exposed parameter, landed
// now or queued as automation by turns, the newest bundle left for the next
// block or every one taken through no port; notes, a controller, a bend and a
// split SysEx at their timestamps, then the render.
template <typename P>
void busyBlock(AdapterHost<P>& host,
               const std::vector<std::string>& ids,
               int block,
               int frames)
{
    auto normalized = block % 2 == 0 ? 0.3 : 0.7;
    host.controller.automationTimestamp = block % 3 == 0 ? 0 : 1000 + block;

    for (const auto& id: ids)
        host.adapter->UpdateParameterNormalizedValue(
            id.data(), normalized, AAX_eUpdateSource_Unspecified);

    host.adapter->GenerateCoefficients();
    host.controller.automationTimestamp = 0;
    host.deliveredState = std::max<int64_t>(0, host.lastPosted() - 1);
    host.context.parameterState = block % 5 == 4 ? nullptr : &host.deliveredState;

    host.midiIn.clear();
    host.midiOut.clear();

    auto last = static_cast<uint32_t>(std::max(frames, 1) - 1);
    host.midiIn.add(0, {0x90, 60, 100});
    host.midiIn.add(last / 3, {0xb0, 1, 64});
    host.midiIn.add(last / 2, {0xf0, 0x7e, 0x7f, 0x09});
    host.midiIn.add(last / 2, {0x01, 0xf7});
    host.midiIn.add(last * 2 / 3, {0xe0, 0, 80});
    host.midiIn.add(last, {0x80, 60, 0});

    host.render(frames);
}

template <typename P>
void busyBlock(AdapterHost<P>& host, int block, int frames)
{
    busyBlock(host, exposedIds(host), block, frames);
}

// One unmeasured busy block, then seven measured ones of varying sizes, two
// past the maximum and an empty one among them.
template <typename P>
int allocationsOverBlocks(AdapterHost<P>& host)
{
    auto ids = exposedIds(host);
    auto count = 0;
    auto block = 0;

    busyBlock(host, ids, block++, TestPlugins::blockSize);

    for (auto size: {64, 17, 1, 63, 1024, 2500, 0})
        count += allocationsIn([&] { busyBlock(host, ids, block++, size); });

    return count;
}

// Automation queued past the queue's capacity, the rest landing early, then one
// block that takes it all.
template <typename P>
int allocationsOverFlood(AdapterHost<P>& host)
{
    auto ids = exposedIds(host);

    if (ids.empty())
        return 0;

    return allocationsIn(
        [&]
        {
            host.controller.automationTimestamp = 1000;

            for (auto i = 0; i < Format::Adapter::queueCapacity + 8; ++i)
            {
                host.adapter->UpdateParameterNormalizedValue(
                    ids.front().data(),
                    i % 2 == 0 ? 0.25 : 0.75,
                    AAX_eUpdateSource_Unspecified);
                host.adapter->GenerateCoefficients();
            }

            host.controller.automationTimestamp = 0;
            host.deliverLatest();
            host.render(TestPlugins::blockSize);
        });
}

// The same while every parameter is held by an editor gesture, and after the
// gestures end, the gestures measured too.
template <typename P>
void checkHeldRenderIsOffTheHeap(AdapterHost<P>& host, std::string_view what = {})
{
    auto& listener = *host.plugin().hostEditListener();
    auto numParams = host.plugin().parameters().size();

    nano::check(allocationsOverBlocks(host) == 0, what);

    nano::check(allocationsIn(
                    [&]
                    {
                        for (auto i = 0; i < numParams; ++i)
                        {
                            listener.beginParameterEdit(i);
                            listener.performParameterEdit(i, 0.5f);
                        }
                    })
                    == 0,
                what);

    nano::check(allocationsOverBlocks(host) == 0, what);

    nano::check(allocationsIn(
                    [&]
                    {
                        for (auto i = 0; i < numParams; ++i)
                            listener.endParameterEdit(i);
                    })
                    == 0,
                what);

    nano::check(allocationsOverBlocks(host) == 0, what);
}

} // namespace AAXAllocation
