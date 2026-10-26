#include "StandaloneProcessor.h"

#include <algorithm>
#include <cmath>

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
{
    return wrapper.busLayout();
}

void StandaloneProcessor::prepare(const ProcessSpec& spec)
{
    auto inputChannels = totalChannels(spec.layout.inputs);

    inputTable.resize(inputChannels);
    outputTable.resize(totalChannels(spec.layout.outputs));
    silence.setSize(inputChannels, std::max(1, spec.maxBlockSize));
    silence.clear();

    // Typed while no stream was running would otherwise all land in the first block.
    auto stale = MIDI::Event {};

    while (injected.pop(stale))
    {
    }

    // A tone sounding when the stream restarts is dropped, not resumed at
    // another rate.
    toneRate = spec.sampleRate;
    toneLength = static_cast<int>(spec.sampleRate);
    tonePosition = toneLength;

    wrapper.prepare(spec.sampleRate, spec.maxBlockSize);
}

void StandaloneProcessor::reset() noexcept
{
    wrapper.reset();
}

bool StandaloneProcessor::injectMidi(const MIDI::Event& event) noexcept
{
    return injected.push(event);
}

void StandaloneProcessor::setInputMuted(bool muted) noexcept
{
    inputMuted.store(muted, std::memory_order_relaxed);
}

bool StandaloneProcessor::isInputMuted() const noexcept
{
    return inputMuted.load(std::memory_order_relaxed);
}

void StandaloneProcessor::playTestTone() noexcept
{
    toneRequested.store(true, std::memory_order_relaxed);
}

void StandaloneProcessor::addTestTone(ProcessContext& context) noexcept
{
    if (toneRequested.exchange(false, std::memory_order_relaxed))
        tonePosition = 0;

    if (tonePosition >= toneLength || context.outputs.empty())
        return;

    constexpr auto frequency = 440.0;
    constexpr auto amplitude = 0.5;
    constexpr auto twoPi = 6.283185307179586;

    auto fadeIn = std::max(1, toneLength / 10);
    auto fadeOut = std::max(1, toneLength / 4);
    auto numSamples = context.outputs[0].getNumSamples();
    auto played = std::min(numSamples, toneLength - tonePosition);

    for (auto s = 0; s < played; ++s)
    {
        auto position = tonePosition + s;
        auto gain = std::min({1.0,
                              (double) position / fadeIn,
                              (double) (toneLength - position) / fadeOut});
        auto sample = static_cast<float>(
            amplitude * gain * std::sin(twoPi * frequency * position / toneRate));

        for (auto& buffer: context.outputs)
            for (auto ch = 0; ch < buffer.getNumChannels(); ++ch)
                if (s < buffer.getNumSamples())
                    buffer.getChannelPointer(ch)[s] += sample;
    }

    tonePosition += played;
}

void StandaloneProcessor::process(ProcessContext& context) noexcept
{
    pushMidiIn(context);
    wrapper.setPlayhead(context.playhead);
    bindBuses(context);
    wrapper.process();
    addTestTone(context);

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

    // Engine prepares for any block longer than the last, so a muted block
    // fits the silence. One driven by hand that does not is bound as absent
    // channels, which the wrapper silences in its own way: muted is never the
    // device's input.
    auto muted = isInputMuted();

    auto inputStart = 0;

    for (auto bus = 0; bus < context.inputs.size(); ++bus)
    {
        const auto& buffer = context.inputs[bus];
        auto channels = runLength(buffer, inputStart, inputTable.size());
        auto* run = inputTable.data() + inputStart;
        auto fits = buffer.getNumSamples() <= silence.getNumSamples();

        for (auto ch = 0; ch < channels; ++ch)
        {
            if (!muted)
            {
                run[ch] = buffer.getChannelPointer(ch);
            }
            else if (fits)
            {
                auto* zeros = silence.getChannelPointer(inputStart + ch);
                std::fill_n(zeros, buffer.getNumSamples(), 0.f);
                run[ch] = zeros;
            }
            else
            {
                run[ch] = nullptr;
            }
        }

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
