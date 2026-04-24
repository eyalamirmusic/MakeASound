#pragma once

#include "../DeviceInfo/DeviceInfo.h"
#include <memory>

namespace MakeASound
{
namespace RTAudio
{
struct DeviceManager;
}

class DeviceManager
{
public:
    DeviceManager();
    ~DeviceManager();

    std::vector<DeviceInfo> getDevices() const;
    DeviceInfo getDefaultInputDevice() const;
    DeviceInfo getDefaultOutputDevice() const;
    StreamConfig getDefaultConfig() const;

    void setConfig(const StreamConfig& configToUse);
    void start(const StreamConfig& configToUse, const Callback& cb);
    void stop() const;

    long getStreamLatency() const;
    unsigned int getStreamSampleRate() const;

private:
    unsigned int openStream();

    AudioCallbackInfo prevInfo;
    Callback callback;
    StreamConfig config;

    std::unique_ptr<RTAudio::DeviceManager> pimpl;
};

} // namespace MakeASound
