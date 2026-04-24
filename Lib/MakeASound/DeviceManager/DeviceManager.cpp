#include "DeviceManager.h"
#include "../RTAudio/RTAudioDeviceManager.h"

namespace MakeASound
{

DeviceManager::DeviceManager()
    : pimpl(std::make_unique<RTAudio::DeviceManager>())
{
}

DeviceManager::~DeviceManager()
{
    stop();
}

std::vector<DeviceInfo> DeviceManager::getDevices() const
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

    defaultConfig.input = StreamParameters(getDefaultInputDevice(), true);
    defaultConfig.output = StreamParameters(getDefaultOutputDevice(), false);

    defaultConfig.sampleRate = 44100;
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

unsigned int DeviceManager::getStreamSampleRate() const
{
    return pimpl->getStreamSampleRate();
}

unsigned int DeviceManager::openStream()
{
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
