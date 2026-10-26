// The VST3 adapter's audio-thread paths under the allocation ban: a whole
// process() call with automation, notes, shadow-parameter CC and an echoed
// output, over varying block sizes and a reconfiguration; the controller's
// setParamNormalized; and the held path an editor gesture takes. The drivers are
// VST3AllocationDriver.h's, which a consumer's plugin runs through too; what is
// here is the fixtures and the checks their members allow.

#include "VST3AllocationDriver.h"
#include "JuceFixtures.h"
#include "TamberPlugFixtures.h"

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <array>

namespace VST3AllocationTests
{
using namespace nano;
using namespace TestPlugins;
using namespace VST3Host;
using namespace VST3Allocation;

namespace
{
template <typename P>
void checkFixtureProcess(bool inPlace)
{
    auto host = AdapterHost<P> {};
    checkProcessIsOffTheHeap(host, inPlace);
}

auto tEffectProcess = test("Allocations/vst3EffectProcessIsOffTheHeap") = []
{
    for (auto inPlace: {false, true})
        checkFixtureProcess<GainPlugin>(inPlace);

    auto host = AdapterHost<GainPlugin> {};
    allocationsOverBlocks(host, false, maxBlock);
    check(host.plugin().processed > 0);
};

auto tInstrumentProcess = test("Allocations/vst3InstrumentProcessIsOffTheHeap") = []
{
    checkFixtureProcess<SynthPlugin>(false);

    auto host = AdapterHost<SynthPlugin> {{}, withMidiControllers};
    fillBusyBlock(host, maxBlock, false, 0);
    host.run();
    check(host.outputEvents.getEventCount() > 0);
};

// Inactive buses as JUCE hosts pass them: every channel pointer null, which the
// wrapper replaces with scratch prepared at activation.
auto tNullChannels = test("Allocations/vst3NullChannelPointersAreOffTheHeap") = []
{
    auto host = AdapterHost<AbsentChannelsPlugin> {};
    auto nulls = std::vector<float*>(2, nullptr);
    auto count = 0;

    for (auto size: {64, 17, 1, 63, 0})
    {
        fillBusyBlock(host, size, false, size);
        host.inputBuses[1].channelBuffers32 = nulls.data();
        host.outputBuses[1].channelBuffers32 = nulls.data();
        count += allocationsIn([&] { host.run(); });
    }

    // The empty block processes nothing.
    check(count == 0);
    check(host.plugin().processed == 4);
    check(host.plugin().nullChannels == 0);
    check(host.plugin().narrowBuses == 0);
};

auto tSetParamNormalized =
    test("Allocations/vst3SetParamNormalizedIsOffTheHeap") = []
{
    auto effect = AdapterHost<GainPlugin> {};
    auto instrument = AdapterHost<SynthPlugin> {{}, withMidiControllers};
    auto shadow = MakeASound::VST3::shadowIdFor(0, 0, 1);

    check(setParamNormalizedAllocations(effect) == 0);
    check(setParamNormalizedAllocations(instrument) == 0);
    check(instrument.adapter->getParamNormalized(shadow) == 0.75);
};

// The plugin moves every parameter itself between blocks (a preset of its own),
// and each block leaves a point per parameter in the output changes: under
// IdScheme::TamberPlug none, and per parameter's choice some.
auto tPluginSideChanges =
    test("Allocations/vst3PluginSideChangesLeaveOffTheHeap") = []
{
    auto host = AdapterHost<GainPlugin> {};
    check(pluginSideChangesAllocations(host) == 0);

    auto plug = MakeASound::policyFor(MakeASound::IdScheme::TamberPlug, {});
    auto unreported = AdapterHost<GainPlugin> {plug.parameters};
    check(pluginSideChangesAllocations(unreported) == 0);

    auto chosen = AdapterHost<MoveReportPlugin> {plug.parameters};
    check(pluginSideChangesAllocations(chosen) == 0);
};

// A JUCE port's ids, unmasked (2^31 and more, the 'byps' bypass) and by index:
// the queue, the lookups and the plugin's own moves stay off the heap.
auto tJuceIds = test("Allocations/vst3JuceIdsAreOffTheHeap") = []
{
    using MakeASound::IdScheme;
    using MakeASound::policyFor;

    // The fixture's cutoff is 2945903597 unmasked.
    auto unmasked = AdapterHost<JuceFixtures::FixturePlugin> {
        policyFor(IdScheme::JUCE, {.studioOneCompatibleParameters = false})
            .parameters};
    check(allocationsOverBlocks(unmasked, false, maxBlock) == 0);

    auto ids = exposedIds(*unmasked.adapter);
    check(std::any_of(
        ids.begin(), ids.end(), [](auto id) { return id >= 0x80000000u; }));

    const auto& list = unmasked.plugin().parameters();
    auto& wrapper = unmasked.adapter->wrapper();
    auto count = 0;

    for (auto value: {0.2f, 0.8f})
    {
        unmasked.clearQueues();

        for (auto i = 0; i < list.size(); ++i)
            wrapper.setNormalizedParameter(i, value);

        unmasked.fillBlock(maxBlock);
        count += allocationsIn([&] { unmasked.run(); });
        check(unmasked.outputChanges.getParameterCount()
              == static_cast<Steinberg::int32>(ids.size()));
    }

    count += allocationsIn(
        [&]
        {
            for (auto id: ids)
                unmasked.adapter->setParamNormalized(id, 0.5);
        });

    check(count == 0);

    auto legacy = AdapterHost<SynthPlugin> {
        policyFor(IdScheme::JUCELegacyIndex, {}).parameters, withMidiControllers};
    check(allocationsOverBlocks(legacy, false, maxBlock) == 0);
};

// A Tamber Plug port's index ids, a post-port FNV one among them, and the
// added bypass: the queue, the lookups and the plugin's moves stay off the heap.
auto tPlugIds = test("Allocations/vst3TamberPlugIdsAreOffTheHeap") = []
{
    using MakeASound::IdScheme;

    auto identity = MakeASound::policyFor(IdScheme::TamberPlug, {}).parameters;
    identity.indexedThroughVersion = 1;

    auto host = AdapterHost<TamberPlugFixtures::NewerSVFPort> {identity};
    check(allocationsOverBlocks(host, false, maxBlock) == 0);

    auto ids = exposedIds(*host.adapter);
    auto count = allocationsIn(
        [&]
        {
            for (auto id: ids)
                host.adapter->setParamNormalized(id, 0.5);
        });
    check(count == 0);
};

auto tHeldPath = test("Allocations/vst3HeldParameterPathIsOffTheHeap") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto numParams = host.plugin().parameters().size();

    check(heldPathAllocations(host) == 0);
    check(host.handler.edits.size() == static_cast<size_t>(numParams) * 2);
};
} // namespace
} // namespace VST3AllocationTests
