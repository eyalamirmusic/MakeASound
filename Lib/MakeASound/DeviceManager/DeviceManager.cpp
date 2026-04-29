#include "DeviceManager.h"
#include "../RTAudio/RTAudioDeviceManager.h"

#include <algorithm>
#include <stdexcept>

namespace MakeASound
{
namespace
{
bool deviceSupports(const DeviceInfo& device, int rate)
{
    return std::ranges::find(device.sampleRates, rate) != device.sampleRates.end();
}

int pickDefaultSampleRate(const DeviceInfo& output, const DeviceInfo& input)
{
    auto isCommon = [&](int rate)
    { return deviceSupports(output, rate) && deviceSupports(input, rate); };

    if (output.preferredSampleRate > 0 && isCommon(output.preferredSampleRate))
        return output.preferredSampleRate;

    if (input.preferredSampleRate > 0 && isCommon(input.preferredSampleRate))
        return input.preferredSampleRate;

    auto best = 0;
    for (auto rate: output.sampleRates)
        if (rate > best && isCommon(rate))
            best = rate;

    if (best > 0)
        return best;

    if (output.preferredSampleRate > 0)
        return output.preferredSampleRate;

    if (!output.sampleRates.empty())
        return output.sampleRates.front();

    return 44100;
}
} // namespace

DeviceManager::DeviceManager()
    : pimpl(EA::makeOwned<RTAudio::DeviceManager>())
{
}

DeviceManager::~DeviceManager()
{
    stop();
}

Vector<DeviceInfo> DeviceManager::getDevices() const
{
    return pimpl->getDevices();
}

DeviceInfo DeviceManager::getDefaultInputDevice() const
{
    return pimpl->getDefaultInputDevice();
}

DeviceInfo DeviceManager::getDefaultOutputDevice() const
{
    return pimpl->getDefaultOutputDevice();
}

StreamConfig DeviceManager::getDefaultConfig() const
{
    auto defaultConfig = StreamConfig();

    auto input = getDefaultInputDevice();
    auto output = getDefaultOutputDevice();

    defaultConfig.input = StreamParameters(input, true);
    defaultConfig.output = StreamParameters(output, false);

    defaultConfig.sampleRate = pickDefaultSampleRate(output, input);
    defaultConfig.maxBlockSize = 512;

    return defaultConfig;
}

void DeviceManager::setConfig(const StreamConfig& configToUse)
{
    stop();
    config = configToUse;
    openStream();
}

void DeviceManager::start(const StreamConfig& configToUse, const Callback& cb)
{
    callback = cb;
    setConfig(configToUse);
}

void DeviceManager::stop() const
{
    pimpl->stop();
}

long DeviceManager::getStreamLatency() const
{
    return pimpl->getStreamLatency();
}

int DeviceManager::getStreamSampleRate() const
{
    return pimpl->getStreamSampleRate();
}

int DeviceManager::openStream()
{
    if (!callback)
        throw std::runtime_error("MakeASound: openStream called without a callback");

    auto actualCallback = [this](AudioCallbackInfo& info)
    {
        if (prevInfo != info)
        {
            prevInfo = info;
            info.dirty = true;
        }

        callback(info);
    };

    pimpl->callback = actualCallback;
    auto res = pimpl->openStream(config);
    pimpl->start();

    return res;
}

} // namespace MakeASound
