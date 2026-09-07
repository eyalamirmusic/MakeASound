#include "AudioEngine.h"

#include <cmath>
#include <numbers>

namespace AudioProbe
{
namespace
{
constexpr auto twoPi = 2.f * std::numbers::pi_v<float>;

// Harmonics rather than a bare sine: one partial draws a single thin line, and
// a picture with nothing in it says nothing about the picture.
constexpr float harmonicGains[] = {1.f, 0.45f, 0.22f, 0.11f};
} // namespace

AudioEngine::AudioEngine()
{
    before = snapshotSession();
    manager.emplace();
    after = snapshotSession();

    config = manager->getDefaultConfig();

    // A tone generator has nothing to capture, and on iOS the capture side is
    // what turns the session into PlayAndRecord and asks for the microphone.
    config.input.reset();
    config.maxBlockSize = 256;

    manager->setNotificationCallback(
        [this](MS::DeviceNotification notification)
        { events.push({notification, std::this_thread::get_id() == mainThread}); });
}

AudioEngine::~AudioEngine()
{
    if (manager.has_value())
        manager->stop();
}

MS::Error AudioEngine::start()
{
    lastError = manager->start(config, [this](auto& info) { audioCallback(info); });
    return lastError;
}

MS::Error AudioEngine::reopen()
{
    lastError = manager->setConfig(config);
    return lastError;
}

void AudioEngine::setOutputDevice(int deviceId)
{
    for (auto& device: manager->getDevices())
    {
        if (device.id != deviceId || device.outputChannels == 0)
            continue;

        config.output = MS::StreamParameters {device, false};

        if (!MS::deviceSupportsSampleRate(device, config.sampleRate)
            && !device.sampleRates.empty())
            config.sampleRate = device.sampleRates.front();

        reopen();
        return;
    }
}

void AudioEngine::setSampleRate(int rate)
{
    config.sampleRate = rate;
    reopen();
}

void AudioEngine::setBlockSize(int size)
{
    config.maxBlockSize = size;
    reopen();
}

void AudioEngine::setInputEnabled(bool enabled)
{
    if (!enabled)
    {
        config.input.reset();
        reopen();
        return;
    }

    auto device = manager->getDefaultInputDevice();

    if (!device.hasChannels(true))
        return;

    config.input = MS::StreamParameters {device, true};
    reopen();
}

void AudioEngine::setAutoRecover(bool enabled)
{
    autoRecover = enabled;
    manager->setAutoRecover(enabled);
}

StreamStats AudioEngine::getStats() const
{
    auto stats = StreamStats {};
    stats.sampleRate = statSampleRate.load();
    stats.blockSize = statBlockSize.load();
    stats.lastNumSamples = statNumSamples.load();
    stats.inputs = statInputs.load();
    stats.outputs = statOutputs.load();
    stats.latency = statLatency.load();
    stats.callbacks = statCallbacks.load();
    stats.dirtyBlocks = statDirty.load();
    stats.underflows = statUnderflows.load();
    stats.overflows = statOverflows.load();
    stats.firstBlockDirty = statFirstBlockDirty.load();

    return stats;
}

MS::Vector<DeviceEvent> AudioEngine::drainEvents()
{
    auto drained = MS::Vector<DeviceEvent> {};
    auto event = DeviceEvent {};

    while (events.pop(event))
        drained.add(event);

    return drained;
}

void AudioEngine::audioCallback(MS::AudioCallbackInfo& info)
{
    auto output = info.getOutput();
    auto input = info.getInput();

    auto rate = info.sampleRate > 0 ? static_cast<float>(info.sampleRate) : 48000.f;
    auto hz = toneHz.load(std::memory_order_relaxed);
    auto gain = levelGain.load(std::memory_order_relaxed);

    // The input reaches the analyser and never the output: monitoring a phone's
    // own microphone through its own speaker is a feedback loop.
    auto listening = monitorInput.load(std::memory_order_relaxed)
                     && info.numInputs > 0 && !input.isEmpty();

    auto increment = twoPi * hz / rate;

    for (auto i = 0; i < info.numSamples; ++i)
    {
        auto tone = 0.f;

        for (auto harmonic = 0; harmonic < 4; ++harmonic)
            tone += std::sin(phase * static_cast<float>(harmonic + 1))
                    * harmonicGains[harmonic];

        noiseState = noiseState * 1664525u + 1013904223u;
        auto noise = static_cast<float>(noiseState >> 8) / 8388608.f - 1.f;

        auto value = (tone * 0.25f + noise * 0.02f) * gain;

        phase += increment;

        if (phase >= twoPi)
            phase -= twoPi;

        for (auto channel: output)
            channel[i] = value;

        analyser.push(listening ? input.getChannel(0)[i] : value);
    }

    if (info.dirty)
    {
        if (statCallbacks.load(std::memory_order_relaxed) == 0)
            statFirstBlockDirty.store(true, std::memory_order_relaxed);

        statDirty.fetch_add(1, std::memory_order_relaxed);
    }

    if (info.status == MS::AudioCallbackStatus::OutputUnderflow)
        statUnderflows.fetch_add(1, std::memory_order_relaxed);

    if (info.status == MS::AudioCallbackStatus::InputOverflow)
        statOverflows.fetch_add(1, std::memory_order_relaxed);

    statSampleRate.store(info.sampleRate, std::memory_order_relaxed);
    statBlockSize.store(info.maxBlockSize, std::memory_order_relaxed);
    statNumSamples.store(info.numSamples, std::memory_order_relaxed);
    statInputs.store(info.numInputs, std::memory_order_relaxed);
    statOutputs.store(info.numOutputs, std::memory_order_relaxed);
    statLatency.store(info.latency, std::memory_order_relaxed);
    statCallbacks.fetch_add(1, std::memory_order_relaxed);
}

} // namespace AudioProbe
