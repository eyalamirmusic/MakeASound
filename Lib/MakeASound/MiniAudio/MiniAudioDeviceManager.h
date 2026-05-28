#pragma once

#include "MiniAudio-Backend.h"

namespace MakeASound::MiniAudio
{

void audioCallback(ma_device* device,
                   void* output,
                   const void* input,
                   ma_uint32 frameCount);

struct DeviceManager
{
    DeviceManager();
    ~DeviceManager();

    Vector<DeviceInfo> getDevices();
    DeviceInfo getDefaultInputDevice();
    DeviceInfo getDefaultOutputDevice();

    void start();
    void stop();
    int openStream(const StreamConfig& configToUse);

    long getStreamLatency() const;
    int getStreamSampleRate() const;

    void onCallback(void* output, const void* input, ma_uint32 frameCount);

    Callback callback;
    StreamConfig config;

private:
    DeviceInfo buildDeviceInfo(const ma_device_info& enumInfo,
                               ma_device_type type,
                               int assignedId);
    void refreshDeviceCache();
    const ma_device_id* findDeviceId(int makeASoundId) const;

    ma_context context {};
    bool contextInitialised = false;

    ma_device device {};
    bool deviceInitialised = false;

    struct CachedDevice
    {
        int id {};
        ma_device_id playbackId {};
        ma_device_id captureId {};
        bool hasPlayback = false;
        bool hasCapture = false;
        DeviceInfo info {};
    };

    Vector<CachedDevice> deviceCache;

    Vector<float> inputScratch;
    Vector<float> outputScratch;

    ma_uint64 framesElapsed = 0;
};

} // namespace MakeASound::MiniAudio
