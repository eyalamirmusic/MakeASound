#include "StandaloneProcessor.h"

#include <algorithm>

namespace MakeASound::Standalone
{

namespace
{
int totalChannels(const Vector<Bus>& buses)
{
    auto total = 0;

    for (const auto& bus: buses)
        total += bus.numChannels;

    return total;
}
} // namespace

StandaloneProcessor::StandaloneProcessor(PluginWrapper& wrapperToUse,
                                         MidiSender& senderToUse)
    : wrapper(wrapperToUse)
    , sender(senderToUse)
{
}

BusLayout StandaloneProcessor::getBusLayout() const
{ return wrapper.busLayout(); }

void StandaloneProcessor::prepare(const ProcessSpec& spec)
{
    inputTable.resize(totalChannels(spec.layout.inputs));
    outputTable.resize(totalChannels(spec.layout.outputs));

    // Typed while no stream was running would otherwise all land in the first block.
    auto stale = MIDI::Event {};

    while (injected.pop(stale)) {}

    wrapper.prepare(spec.sampleRate, spec.maxBlockSize);
}

void StandaloneProcessor::reset() noexcept
{ wrapper.reset(); }

bool StandaloneProcessor::injectMidi(const MIDI::Event& event) noexcept
{ return injected.push(event); }

void StandaloneProcessor::process(ProcessContext& context) noexcept
{
    pushMidiIn(context);
    wrapper.setPlayhead(context.playhead);
    bindBuses(context);
    wrapper.process();

    if (!wrapper.midiOut().empty())
        for (const auto& event: wrapper.midiOut()[0])
            sender.push(event);
}

void StandaloneProcessor::pushMidiIn(ProcessContext& context) noexcept
{
    wrapper.clearMidi();

    for (auto bus = 0; bus < context.midiIn.size(); ++bus)
        for (const auto& event: context.midiIn[bus])
            wrapper.pushMidiIn(bus, event);

    auto event = MIDI::Event {};

    while (injected.pop(event))
    {
        event.sampleOffset = 0;
        wrapper.pushMidiIn(0, event);
    }

    wrapper.sortMidiInByOffset();
}

void StandaloneProcessor::bindBuses(ProcessContext& context) noexcept
{
    // Each run is clamped to what prepare() sized, so a context wider than the
    // layout binds fewer channels rather than writing past the table.
    auto runLength = [](const Buffer& buffer, int start, int tableSize)
    { return std::clamp(buffer.getNumChannels(), 0, tableSize - start); };

    auto inputStart = 0;

    for (auto bus = 0; bus < context.inputs.size(); ++bus)
    {
        const auto& buffer = context.inputs[bus];
        auto channels = runLength(buffer, inputStart, inputTable.size());
        auto* run = inputTable.data() + inputStart;

        for (auto ch = 0; ch < channels; ++ch)
            run[ch] = buffer.getChannelPointer(ch);

        wrapper.bindInput(bus, run, channels, buffer.getNumSamples());
        inputStart += channels;
    }

    inputStart = 0;
    auto outputStart = 0;

    for (auto bus = 0; bus < context.outputs.size(); ++bus)
    {
        auto& buffer = context.outputs[bus];
        auto channels = runLength(buffer, outputStart, outputTable.size());
        auto* run = outputTable.data() + outputStart;

        for (auto ch = 0; ch < channels; ++ch)
            run[ch] = buffer.getChannelPointer(ch);

        const float* const* matching = nullptr;
        auto matchingChannels = 0;

        if (bus < context.inputs.size())
        {
            matchingChannels =
                runLength(context.inputs[bus], inputStart, inputTable.size());
            matching = inputTable.data() + inputStart;
            inputStart += matchingChannels;
        }

        wrapper.bindOutput(
            bus, run, channels, buffer.getNumSamples(), matching, matchingChannels);
        outputStart += channels;
    }
}

} // namespace MakeASound::Standalone
