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

    // The device's whole rate becomes the spec's double here, and only here. Every
    // int is exact as a double, so the comparisons against the device's rate in
    // start() and process() are exact too.
    spec = {static_cast<double>(sampleRate),
            maxBlockSize,
            processorToUse.getBusLayout()};

    context.prepare(spec.layout);
    inputTable.resize(spec.layout.getMainInputChannels());
    outputTable.resize(spec.layout.getMainOutputChannels());
    silence.setSize(1, maxBlockSize);
    bin.setSize(1, maxBlockSize);
    midiSync.reset();

    restartPending = false;
    pendingSampleRate = 0;
    pendingBlockSize = 0;

    processorToUse.prepare(spec);
}

Error Engine::start(const StreamConfig& configToUse, Processor& processorToUse)
{
    config = configToUse;

    if (config.maxBlockSize <= 0)
        config.maxBlockSize = defaultBlockSize;

    return open(processorToUse, config.sampleRate, config.maxBlockSize, 0);
}

Error Engine::restart()
{
    if (processor == nullptr)
        return Error::INVALID_USE;

    auto rate = pendingSampleRate.load();
    auto block = pendingBlockSize.load();

    // The largest block seen is also a floor for what the device then reports:
    // one that claims less than it delivers would otherwise ask again forever.
    return open(*processor,
                rate > 0 ? rate : config.sampleRate,
                block > 0 ? block : config.maxBlockSize,
                block);
}

Error Engine::open(Processor& processorToUse,
                   int sampleRate,
                   int maxBlockSize,
                   int minBlockSize)
{
    devices.stop();

    auto streamConfig = config;

    if (processorToUse.getBusLayout().inputs.empty())
        streamConfig.input.reset();

    prepare(processorToUse, sampleRate, maxBlockSize);

    auto callback = [this](auto& info) { process(info); };
    auto error = devices.start(streamConfig, callback);

    if (error != Error::NoError)
        return error;

    // The device may have settled on another rate or period than it was asked
    // for; preparing for what it runs here keeps the first callback from
    // finding a spec it does not fit.
    auto rate = devices.getStreamSampleRate();
    auto block = std::max(devices.getStreamBlockSize(), minBlockSize);

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

    restartPending = false;
    pendingSampleRate = 0;
    pendingBlockSize = 0;
}

bool Engine::takeRestartRequest() noexcept
{
    return restartPending.exchange(false);
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

    // Checked on every block, not only dirty ones: a stream re-opened in the
    // shape of the one before it raises no dirty flag. Re-preparing here would
    // run the processor's prepare() on the audio thread, so the block is
    // silenced and the host thread asked to restart instead. Its MIDI is drained
    // and dropped, so it cannot land in a burst on the first block back.
    if (!fits(info))
    {
        requestRestart(info);
        info.getOutput().clear();
        midiSync.drainForBlock(midi, info.numSamples, info.sampleRate);
        silenced = true;
        return;
    }

    if (info.dirty || silenced)
    {
        processor->reset();
        midiSync.reset();
        silenced = false;
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

// A device maximum below the spec's fits: every block it can deliver does.
bool Engine::fits(const AudioCallbackInfo& info) const noexcept
{
    return info.sampleRate == spec.sampleRate
           && info.maxBlockSize <= spec.maxBlockSize
           && info.numSamples <= spec.maxBlockSize;
}

// The only writer while the stream runs; the host thread clears these with the
// stream stopped.
void Engine::requestRestart(const AudioCallbackInfo& info) noexcept
{
    auto block = std::max({info.maxBlockSize,
                           info.numSamples,
                           pendingBlockSize.load(std::memory_order_relaxed)});

    pendingSampleRate.store(info.sampleRate, std::memory_order_relaxed);
    pendingBlockSize.store(block, std::memory_order_relaxed);
    restartPending.store(true, std::memory_order_release);
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
