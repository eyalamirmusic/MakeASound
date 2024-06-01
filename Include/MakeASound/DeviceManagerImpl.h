#pragma once

#include "DeviceManager.h"
#include "RtAudio/RTAudioDeviceManager.h"

namespace MakeASound
{
using RT = RTAudio::DeviceManager;

inline DeviceManager::DeviceManager()
{
    pimpl = std::make_shared<RT>();
}
inline DeviceManager::~DeviceManager()
{
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
    return getConcrete<RT>().getDefaultConfig();
}
inline void DeviceManager::setConfig(const StreamConfig& configToUse)
{
    return getConcrete<RT>().setConfig(configToUse);
}
inline void DeviceManager::start(const StreamConfig& configToUse, const Callback& cb)
{
    return getConcrete<RT>().start(configToUse, cb);
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
} // namespace MakeASound