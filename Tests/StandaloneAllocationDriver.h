#pragma once

// The standalone format's block under the allocation ban, over any plugin: a
// StandaloneProcessor on Engine driven by hand, as a device's callback drives
// it. What StandaloneAllocationTests.cpp measures with its fixtures, and what
// makeasound_add_allocation_tests measures over every class of a consumer's
// describeModule() (PluginAllocations/Standalone.cpp).

#include "AllocationProbe.h"

#include <MakeASound/MakeASound.h>
#include <MakeASound/Plugin/MakeASoundPlugin.h>
#include <MakeASound/Plugin/Standalone/StandaloneProcessor.h>

#include <array>
#include <functional>

namespace StandaloneAllocation
{
using namespace MakeASound;
using Probe::allocationsIn;

constexpr auto blockSize = 64;

// The callback a device would hand Engine: two inputs, two outputs, clean.
struct StandaloneStream
{
    std::array<float, blockSize> inLeft = {};
    std::array<float, blockSize> inRight = {};
    std::array<float, blockSize> outLeft = {};
    std::array<float, blockSize> outRight = {};

    std::array<float*, 2> inputs {inLeft.data(), inRight.data()};
    std::array<float*, 2> outputs {outLeft.data(), outRight.data()};

    AudioCallbackInfo info(bool dirty)
    {
        auto result = AudioCallbackInfo {};
        result.numInputs = 2;
        result.numOutputs = 2;
        result.inputChannels = inputs.data();
        result.outputChannels = outputs.data();
        result.numSamples = blockSize;
        result.sampleRate = 48000;
        result.maxBlockSize = blockSize;
        result.dirty = dirty;
        return result;
    }
};

// One unmeasured, dirty block, then a clean one measured, with a note injected
// into the processor's queue between them when `injectNote` (it is echoed to a
// MIDI out bus and into the sender). `after` sees the wrapper before it goes.
// With `muteInput`, both blocks bind the processor's silence in place of the
// device's input (StandaloneHost::setInputMuted).
inline int blockAllocations(OwningPointer<Plugin> plugin,
                            ParameterIdentity identity,
                            bool injectNote,
                            const std::function<void(PluginWrapper&)>& after = {},
                            bool muteInput = false)
{
    auto devices = DeviceManager {};
    auto midi = MidiManager {};
    auto engine = Engine {devices, midi};
    auto sender = Standalone::MidiSender {midi};
    auto wrapper =
        PluginWrapper(std::move(plugin), PluginFormat::Standalone, identity);
    auto processor = Standalone::StandaloneProcessor {wrapper, sender};
    auto stream = StandaloneStream {};

    processor.setInputMuted(muteInput);

    engine.prepare(processor, 48000, blockSize);

    auto first = stream.info(true);
    engine.process(first);

    if (injectNote)
        processor.injectMidi(MIDI::Event::noteOn(0, 60, 1.f));

    auto block = stream.info(false);
    auto count = allocationsIn([&] { engine.process(block); });

    if (after)
        after(wrapper);

    return count;
}

// A new instance of `description`'s plugin, as the standalone app makes its
// own: created under the format, wrapped under its description's identity, and
// given a note when it declares a MIDI input; its input muted with `muteInput`.
inline int blockAllocations(const PluginDescription& description,
                            bool muteInput = false)
{
    setCurrentPluginFormat(PluginFormat::Standalone);
    auto plugin = description.create();
    auto takesMidi = !plugin->getBusLayout().midiInputs.empty();

    return blockAllocations(std::move(plugin),
                            parameterIdentityFor(description),
                            takesMidi,
                            {},
                            muteInput);
}

} // namespace StandaloneAllocation
