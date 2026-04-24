#include "DeviceManager.h"
#include "../RTAudio/RTAudioDeviceManager.h"

namespace MakeASound
{
using RT = RTAudio::DeviceManager;

DeviceManager::DeviceManager()
{
    pimpl = std::make_shared<RT>();
}

DeviceManager::~DeviceManager()
{
    stop();
}

std::vector<DeviceInfo> DeviceManager::getDevices()
{
    return getConcrete<RT>().getDevices();
}

DeviceInfo DeviceManager::getDefaultInputDevice()
{
    return getConcrete<RT>().getDefaultInputDevice();
}

DeviceInfo DeviceManager::getDefaultOutputDevice()
{
    return getConcrete<RT>().getDefaultOutputDevice();
}

StreamConfig DeviceManager::getDefaultConfig()
{
    StreamConfig defaultConfig;

    defaultConfig.input = StreamParameters(getDefaultInputDevice(), true);
    defaultConfig.output = StreamParameters(getDefaultInputDevice(), false);

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

void DeviceManager::stop()
{
    return getConcrete<RT>().stop();
}

long DeviceManager::getStreamLatency()
{
    return getConcrete<RT>().getStreamLatency();
}

unsigned int DeviceManager::getStreamSampleRate()
{
    return getConcrete<RT>().getStreamSampleRate();
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

    auto& rt = getConcrete<RT>();

    rt.callback = actualCallback;
    auto res = rt.openStream(config);
    rt.start();

    return res;
}

} // namespace MakeASound
