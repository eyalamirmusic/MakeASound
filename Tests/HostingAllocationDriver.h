#pragma once

// A hosted plugin's audio-thread paths under the allocation ban, over any
// HostedPlugin, VST3 or Audio Unit: what HostingAllocationTests.cpp and
// HostingAUAllocationTests.cpp measure over the fixtures and the test module's
// units, and what makeasound_add_allocation_tests measures over a consumer's
// own bundle and units (PluginAllocations/HostedVST3.cpp, HostedAU.cpp). The
// host side is measured together with the plugin it hosts.

#include "AllocationProbe.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <algorithm>

namespace HostingAllocation
{
using Probe::allocationsIn;

// A context sized for the plugin's layout, and the plugin prepared for it at
// 48 kHz.
inline MakeASound::ProcessContext
    contextFor(MakeASound::Hosting::HostedPlugin& plugin, int blockSize)
{
    auto context = MakeASound::ProcessContext();
    const auto layout = plugin.getBusLayout();
    context.prepare(layout);

    for (auto i = 0; i < layout.inputs.size(); ++i)
        context.inputs[i].setSize(layout.inputs[i].numChannels, blockSize);

    for (auto i = 0; i < layout.outputs.size(); ++i)
        context.outputs[i].setSize(layout.outputs[i].numChannels, blockSize);

    plugin.prepare(
        {.sampleRate = 48000, .maxBlockSize = blockSize, .layout = layout});
    return context;
}

// The plugin's first blocks, unmeasured: they may settle anything lazy of its
// own.
inline void warmUp(MakeASound::Hosting::HostedPlugin& plugin,
                   MakeASound::ProcessContext& context)
{
    for (auto i = 0; i < 4; ++i)
        plugin.process(context);
}

// Thirty-two measured blocks, each after `eachBlock(block)` fills what the host
// hands it, inside the measurement.
template <typename EachBlock>
int allocationsOverBlocks(MakeASound::Hosting::HostedPlugin& plugin,
                          MakeASound::ProcessContext& context,
                          EachBlock&& eachBlock)
{
    return allocationsIn(
        [&]
        {
            for (auto block = 0; block < 32; ++block)
            {
                eachBlock(block);
                plugin.process(context);
            }
        });
}

// Everything a busy host block carries, for any plugin: a playing fallback
// playhead, edits to every parameter queued from the message thread and drained
// inside the measured blocks, audio-thread writes to the first two parameters,
// and where the plugin takes MIDI a note on, a controller and a note off.
inline int busyBlocksAllocations(MakeASound::Hosting::HostedPlugin& plugin,
                                 int blockSize)
{
    auto context = contextFor(plugin, blockSize);
    auto playing = MakeASound::Playhead();
    playing.isPlaying = true;
    plugin.setFallbackPlayhead(playing);

    warmUp(plugin, context);

    auto& params = plugin.parameters();

    for (auto i = 0; i < params.size(); ++i)
        params[i].setValue(0.25f);

    auto written = std::min(2, params.size());
    auto takesMidi = !context.midiIn.empty();

    return allocationsOverBlocks(
        plugin,
        context,
        [&](int block)
        {
            for (auto i = 0; i < written; ++i)
                plugin.setParameterFromAudioThread(i, (block % 8) / 8.f);

            if (takesMidi)
            {
                auto pitch = 60 + block % 12;
                context.clearMidi();
                context.midiIn[0].add(
                    MakeASound::MIDI::Event::noteOn(0, pitch, 0.7f, 3));
                context.midiIn[0].add(
                    MakeASound::MIDI::Event::controlChange(0, 1, 0.5f, 5));
                context.midiIn[0].add(
                    MakeASound::MIDI::Event::noteOff(0, pitch, 0.f, 9));
            }
        });
}

} // namespace HostingAllocation
