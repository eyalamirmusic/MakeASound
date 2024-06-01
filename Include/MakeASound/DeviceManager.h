#pragma once

#include "DeviceInfo.h"
#include <any>

namespace MakeASound
{

class DeviceManager
{
public:
    DeviceManager();
    ~DeviceManager();

    std::vector<DeviceInfo> getDevices();
    DeviceInfo getDefaultInputDevice();
    DeviceInfo getDefaultOutputDevice();
    StreamConfig getDefaultConfig();

    void setConfig(const StreamConfig& configToUse);
    void start(const StreamConfig& configToUse, const Callback& cb);
    void stop();

    long getStreamLatency();
    unsigned int getStreamSampleRate();

private:
    unsigned int openStream();

    template <typename T>
    T& getConcrete()
    {
        return **std::any_cast<std::shared_ptr<T>>(&pimpl);
    }

    AudioCallbackInfo prevInfo;
    Callback callback;
    StreamConfig config;

    std::any pimpl;
};

} // namespace MakeASound