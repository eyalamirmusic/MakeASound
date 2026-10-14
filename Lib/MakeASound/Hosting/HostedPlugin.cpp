#include "HostedPlugin.h"

namespace MakeASound::Hosting
{

HostedPlugin::HostedPlugin(PluginDescription descriptionToUse)
    : described(std::move(descriptionToUse))
{
}

void HostedPlugin::refreshParameterValue(int)
{
}

bool HostedPlugin::takeParameterInfoChanged() noexcept
{
    return parameterInfoChanged.exchange(false, std::memory_order_acq_rel);
}

bool HostedPlugin::takeIoChanged() noexcept
{
    return ioChanged.exchange(false, std::memory_order_acq_rel);
}

bool HostedPlugin::takeLatencyChanged() noexcept
{
    return latencyChanged.exchange(false, std::memory_order_acq_rel);
}

void HostedPlugin::setFallbackPlayhead(const Playhead& playheadToUse) noexcept
{
    fallback = playheadToUse;
}

void HostedPlugin::raiseParameterInfoChanged() noexcept
{
    parameterInfoChanged.store(true, std::memory_order_release);
}

void HostedPlugin::raiseIoChanged() noexcept
{
    ioChanged.store(true, std::memory_order_release);
}

void HostedPlugin::raiseLatencyChanged() noexcept
{
    latencyChanged.store(true, std::memory_order_release);
}

Playhead HostedPlugin::playheadFor(const Playhead& supplied,
                                   int numSamples,
                                   double sampleRate) noexcept
{
    if (supplied.isValid)
        return supplied;

    auto playhead = fallback;
    playhead.isValid = true;
    playhead.sampleTime = fallbackSampleTime;

    // A tempo-synced delay or LFO needs a musical position even with no
    // transport: the samples so far at the fallback's tempo.
    playhead.ppqPosition = sampleRate > 0.0 ? static_cast<double>(fallbackSampleTime)
                                                  / sampleRate * playhead.bpm / 60.0
                                            : 0.0;

    if (playhead.isPlaying)
        fallbackSampleTime += numSamples;

    return playhead;
}

} // namespace MakeASound::Hosting
