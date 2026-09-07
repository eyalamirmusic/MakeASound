#include "AudioEngine.h"

#include <chrono>
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

// Early enough that a --strict run of a couple of seconds sees the answer, late
// enough that the stream has settled into its own rhythm first.
constexpr auto callbacksBeforeStall = 8;
constexpr auto blocksHeld = 3;
} // namespace

AudioEngine::AudioEngine()
{
    before = MS::getSessionState();
    manager.emplace();
    after = MS::getSessionState();

    // A tone generator has nothing to capture, so it asks for the playback side
    // only — which is what keeps the session off PlayAndRecord and the microphone
    // out of the bundle.
    config = manager->getDefaultOutputConfig();
    config.maxBlockSize = 256;

    manager->setNotificationCallback(
        [this](MS::DeviceNotification notification)
        {
            events.push(
                {notification, false, std::this_thread::get_id() == mainThread});
        });
}

AudioEngine::~AudioEngine()
{
    if (manager.has_value())
        manager->stop();
}

MS::Error AudioEngine::start()
{
    lastError = manager->start(config, [this](auto& info) { audioCallback(info); });
    afterStart = MS::getSessionState();

    return lastError;
}

MS::Error AudioEngine::reopen()
{
    lastError = manager->setConfig(config);
    afterStart = MS::getSessionState();

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

void AudioEngine::setOutputChannels(int firstChannel, int count)
{
    if (!config.output.has_value())
        return;

    config.output->firstChannel = firstChannel;
    config.output->nChannels = count;

    reopen();
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
    stats.stallDone = stalled.load();

    return stats;
}

MS::Vector<DeviceEvent> AudioEngine::drainEvents()
{
    auto drained = MS::Vector<DeviceEvent> {};
    auto event = DeviceEvent {};

    while (events.pop(event))
        drained.add(event);

    // The other half of the same story: the queue MakeASound fills for whatever
    // thread comes and asks, which here is the one drawing the UI.
    auto onMainThread = std::this_thread::get_id() == mainThread;

    for (auto notification: manager->drainNotifications())
        drained.add({notification, true, onMainThread});

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

    auto blocksSoFar = statCallbacks.fetch_add(1, std::memory_order_relaxed);

    if (blocksSoFar == callbacksBeforeStall)
        holdPastTheDeadline(info);
}

// A status nobody can provoke is a status nobody can trust, and nothing else in a
// probe run misses a deadline: this one block is held past its own so the next one
// arrives late enough to be reported as the dropout it is. Costs a single glitch.
void AudioEngine::holdPastTheDeadline(const MS::AudioCallbackInfo& info)
{
    auto rate = info.sampleRate > 0 ? info.sampleRate : 48000;
    auto held = blocksHeld * info.numSamples * 1000000LL / rate;

    std::this_thread::sleep_for(std::chrono::microseconds(held));
    stalled.store(true, std::memory_order_relaxed);
}

} // namespace AudioProbe
