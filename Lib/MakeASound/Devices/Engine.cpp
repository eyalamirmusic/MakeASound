#include "Engine.h"

#include <algorithm>

namespace MakeASound
{

Engine::Engine(DeviceManager& devicesToUse, MidiManager& midiToUse)
    : devices(devicesToUse)
    , midi(midiToUse)
{
}

Engine::~Engine()
{
    stop();
}

void Engine::prepare(Processor& processorToUse, int sampleRate, int maxBlockSize)
{
    processor = &processorToUse;
    spec = {sampleRate, maxBlockSize, processorToUse.getBusLayout()};

    context.prepare(spec.layout);
    inputTable.resize(spec.layout.getMainInputChannels());
    outputTable.resize(spec.layout.getMainOutputChannels());
    silence.setSize(1, maxBlockSize);
    bin.setSize(1, maxBlockSize);
    midiSync.reset();

    processorToUse.prepare(spec);
}

Error Engine::start(const StreamConfig& configToUse, Processor& processorToUse)
{
    devices.stop();

    config = configToUse;

    if (config.maxBlockSize <= 0)
        config.maxBlockSize = defaultBlockSize;

    auto streamConfig = config;

    if (processorToUse.getBusLayout().inputs.empty())
        streamConfig.input.reset();

    prepare(processorToUse, config.sampleRate, config.maxBlockSize);

    auto callback = [this](auto& info) { process(info); };
    auto error = devices.start(streamConfig, callback);

    if (error != Error::NoError)
        return error;

    // The device may have settled on another rate or period than it was asked
    // for; preparing for what it runs here keeps the re-prepare off the first
    // callback.
    auto rate = devices.getStreamSampleRate();
    auto block = devices.getStreamBlockSize();

    if (rate > 0 && block > 0
        && (rate != spec.sampleRate || block != spec.maxBlockSize))
    {
        devices.stop();
        prepare(processorToUse, rate, block);
        error = devices.start(streamConfig, callback);
    }

    return error;
}

void Engine::stop()
{
    devices.stop();
}

bool Engine::isRunning() const
{
    return devices.isRunning();
}

void Engine::process(AudioCallbackInfo& info) noexcept
{
    if (processor == nullptr)
    {
        info.getOutput().clear();
        return;
    }

    if (info.dirty)
    {
        if (info.sampleRate != spec.sampleRate
            || info.maxBlockSize != spec.maxBlockSize
            || info.numSamples > spec.maxBlockSize)
        {
            prepare(*processor,
                    info.sampleRate,
                    std::max(info.maxBlockSize, info.numSamples));
        }

        processor->reset();
        midiSync.reset();
    }

    context.clearMidi();
    midiSync.drainForBlock(midi, info.numSamples, info.sampleRate);

    if (!context.midiIn.empty())
    {
        auto& midiIn = context.midiIn[0];

        for (auto& evt: midiSync.events())
            midiIn.add(evt.event);

        midiIn.sortByOffset();
    }

    bindBuses(info);
    processor->process(context);
}

void Engine::bindBuses(AudioCallbackInfo& info) noexcept
{
    auto numSamples = info.numSamples;

    if (!context.inputs.empty())
    {
        auto channels = spec.layout.getMainInputChannels();
        auto feed = std::min(channels, info.numInputs);

        if (feed == 0)
            silence.getSubBuffer(0, numSamples).clear();

        // A bus wider than the device repeats its first channel, so a mono mic
        // reaches both sides of a stereo bus.
        auto* spare =
            feed > 0 ? info.inputChannels[0] : silence.getChannelPointer(0);

        for (auto ch = 0; ch < channels; ++ch)
            inputTable[ch] = ch < feed ? info.inputChannels[ch] : spare;

        context.inputs[0].referTo(inputTable.data(), channels, numSamples);
    }

    auto busChannels = 0;

    if (!context.outputs.empty())
    {
        busChannels = spec.layout.getMainOutputChannels();
        auto write = std::min(busChannels, info.numOutputs);

        for (auto ch = 0; ch < busChannels; ++ch)
            outputTable[ch] =
                ch < write ? info.outputChannels[ch] : bin.getChannelPointer(0);

        auto& output = context.outputs[0];
        output.referTo(outputTable.data(), busChannels, numSamples);
        output.clear();
    }

    if (info.numOutputs > busChannels)
        info.getOutput()
            .getChannelSubset(busChannels, info.numOutputs - busChannels)
            .clear();
}

} // namespace MakeASound
