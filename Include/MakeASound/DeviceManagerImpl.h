#pragma once

#include "DeviceManager.h"
#include "RTAudio/RTAudioDeviceManager.h"

namespace MakeASound
{
using RT = RTAudio::DeviceManager;

inline DeviceManager::DeviceManager()
{
    pimpl = std::make_shared<RT>();
}
inline DeviceManager::~DeviceManager()
{
    stop();
}

inline std::vector<DeviceInfo> DeviceManager::getDevices()
{
    return getConcrete<RT>().getDevices();
}
inline DeviceInfo DeviceManager::getDefaultInputDevice()
{
    return getConcrete<RT>().getDefaultInputDevice();
}
inline DeviceInfo DeviceManager::getDefaultOutputDevice()
{
    return getConcrete<RT>().getDefaultOutputDevice();
}

inline StreamConfig DeviceManager::getDefaultConfig()
{
    StreamConfig config;

    config.input = StreamParameters(getDefaultInputDevice(), true);
    config.output = StreamParameters(getDefaultInputDevice(), false);

    config.sampleRate = 44100;
    config.maxBlockSize = 512;

    return config;
}

inline void DeviceManager::setConfig(const StreamConfig& configToUse)
{
    stop();
    config = configToUse;
    openStream();
}

inline void DeviceManager::start(const StreamConfig& configToUse, const Callback& cb)
{
    callback = cb;
    setConfig(configToUse);
}

inline void DeviceManager::stop()
{
    return getConcrete<RT>().stop();
}

inline long DeviceManager::getStreamLatency()
{
    return getConcrete<RT>().getStreamLatency();
}
inline unsigned int DeviceManager::getStreamSampleRate()
{
    return getConcrete<RT>().getStreamSampleRate();
}

inline unsigned int DeviceManager::openStream()
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