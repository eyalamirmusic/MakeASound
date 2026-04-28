#pragma once

#include "../Common/Common.h"
#include "../DeviceInfo/DeviceInfo.h"

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

    Vector<DeviceInfo> getDevices() const;
    DeviceInfo getDefaultInputDevice() const;
    DeviceInfo getDefaultOutputDevice() const;
    StreamConfig getDefaultConfig() const;

    void setConfig(const StreamConfig& configToUse);
    void start(const StreamConfig& configToUse, const Callback& cb);
    void stop() const;

    long getStreamLatency() const;
    int getStreamSampleRate() const;

private:
    int openStream();

    AudioCallbackInfo prevInfo;
    Callback callback;
    StreamConfig config;

    OwningPointer<RTAudio::DeviceManager> pimpl;
};

} // namespace MakeASound
