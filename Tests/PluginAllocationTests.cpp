// The plugin core's audio-thread paths under the allocation ban: parameter value
// I/O, host-id lookup, the realtime swap's exchange, and a whole PluginWrapper
// block as an adapter drives it, for an effect and an instrument, in place and
// not. StandaloneAllocationTests.cpp does the same for the standalone format.

#include "AllocationProbe.h"
#include "TestPlugins.h"

#include <NanoTest/NanoTest.h>

#include <array>
#include <chrono>
#include <thread>
#include <type_traits>

using namespace nano;
using namespace TestPlugins;
using Probe::allocationsIn;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
auto tSwapExchange = test("Allocations/realtimeSwapExchangeIsOffTheHeap") = []
{
    auto swap = RealtimeSwap<std::array<float, 1024>>();
    swap.publish();
    swap.currentForBlock();
    swap.publish();

    auto* first = static_cast<std::array<float, 1024>*>(nullptr);
    auto* second = first;

    auto count = allocationsIn(
        [&]
        {
            first = swap.currentForBlock();
            second = swap.currentForBlock();
        });

    check(count == 0);
    check(first != nullptr);
    check(first == second);
};

struct AllocationParams : ParameterGroup
{
    AllocationParams() { add(gain, cutoff, mode, bypass); }

    DecibelParam gain {"Gain", -60.f, 12.f, 0.f};
    HzParam cutoff {"Cutoff", 20.f, 20000.f, 1000.f};
    ChoiceParam mode {"Mode", {"Low", "Band", "High"}, 1};
    BoolParam bypass {"Bypass", false};
};

auto tParameterValues = test("Allocations/parameterValueIoIsOffTheHeap") = []
{
    auto params = AllocationParams {};
    auto list = ParameterList {params};
    auto sum = 0.f;

    auto count = allocationsIn(
        [&]
        {
            for (auto i = 0; i < list.size(); ++i)
            {
                auto& param = list[i];
                param.setValue(param.getValue());
                param.setNormalized(0.3f);
                sum += param.getNormalized();
                sum += param.toPlain(param.toNormalized(param.getValue()));
            }

            sum += params.gain.gain() + params.cutoff.get();
            sum += static_cast<float>(params.mode.getIndex());
            sum += params.bypass.isOn() ? 1.f : 0.f;
        });

    check(count == 0);
    check(sum > 0.f);
};

auto tHostIdLookup = test("Allocations/parameterHostIdLookupIsOffTheHeap") = []
{
    auto params = AllocationParams {};
    auto list = ParameterList {params};
    auto found = 0;

    auto count = allocationsIn(
        [&]
        {
            for (const auto& entry: list)
                found += list.indexOfHostId(entry.hostId) >= 0 ? 1 : 0;

            found += list.indexOfHostId(7u) < 0 ? 1 : 0;
        });

    check(count == 0);
    check(found == list.size() + 1);
};

struct Host
{
    Block inLeft = {};
    Block inRight = {};
    Block outLeft = {};
    Block outRight = {};

    std::array<const float*, 2> inputs {inLeft.data(), inRight.data()};
    std::array<float*, 2> outputs {outLeft.data(), outRight.data()};

    // In place: the output table is the input's.
    std::array<float*, 2> shared {inLeft.data(), inRight.data()};
};

// One block the way an adapter drives it, twice, so the second is steady state.
// Counts the MIDI out events the adapter drained into `drained`.
int allocationsPerBlock(PluginWrapper& wrapper,
                        Host& host,
                        bool inPlace,
                        int& drained)
{
    auto hostId = wrapper.plugin().parameters().entry(0).hostId;
    auto capacity = ProcessContext::defaultMidiCapacity;
    auto playhead = Playhead {};
    playhead.isValid = true;

    auto* outputs = inPlace ? host.shared.data() : host.outputs.data();
    auto* inputs = inPlace ? host.shared.data() : host.inputs.data();

    auto block = [&]
    {
        wrapper.setNormalizedParameter(0, 0.7f);
        wrapper.setParameterByHostId(hostId, 0.6f);
        wrapper.setPlayhead(playhead);
        wrapper.clearMidi();

        for (auto i = 0; i < capacity; ++i)
            wrapper.pushMidiIn(0, MIDI::Event::noteOn(0, 60, 1.f, capacity - i));

        wrapper.sortMidiInByOffset();
        wrapper.bindInput(0, inputs, 2, blockSize);
        wrapper.bindOutput(0, outputs, 2, blockSize, inputs, 2);
        wrapper.process();

        drained = 0;

        for (const auto& bus: wrapper.midiOut())
            drained += bus.size();
    };

    wrapper.prepare(48000, blockSize);
    block();

    return allocationsIn(block);
}

auto tEffectBlock = test("Allocations/pluginWrapperEffectBlockIsOffTheHeap") = []
{
    for (auto inPlace: {false, true})
    {
        auto wrapper = PluginWrapper(EA::makeOwned<GainPlugin>());
        auto host = Host {};
        auto drained = -1;

        check(allocationsPerBlock(wrapper, host, inPlace, drained) == 0);
        check(static_cast<GainPlugin&>(wrapper.plugin()).processed == 2);
        check(drained == 0);
    }
};

auto tInstrumentBlock =
    test("Allocations/pluginWrapperInstrumentBlockIsOffTheHeap") = []
{
    for (auto inPlace: {false, true})
    {
        auto wrapper = PluginWrapper(EA::makeOwned<SynthPlugin>());
        auto host = Host {};
        auto drained = 0;

        check(allocationsPerBlock(wrapper, host, inPlace, drained) == 0);
        check(drained == ProcessContext::defaultMidiCapacity);
    }
};

auto tUnboundBlock = test("Allocations/pluginWrapperUnboundBlockIsOffTheHeap") = []
{
    auto wrapper = PluginWrapper(EA::makeOwned<GainPlugin>());
    auto host = Host {};
    auto drained = 0;
    auto held = wrapper.plugin().parameters().entry(0).hostId;

    allocationsPerBlock(wrapper, host, false, drained);
    wrapper.holdParameter(0);

    auto count = allocationsIn(
        [&]
        {
            wrapper.setParameterByHostId(held, 0.1f);
            wrapper.process();
        });

    check(count == 0);
    check(static_cast<GainPlugin&>(wrapper.plugin()).boundSamples == 0);
};
} // namespace
