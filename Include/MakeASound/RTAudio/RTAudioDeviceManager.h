#pragma once

#include "RTAudio-Backend.h"

namespace MakeASound::RTAudio
{
int audioCallback(void* outputBuffer,
                  void* inputBuffer,
                  unsigned int nFrames,
                  double streamTime,
                  RtAudioStreamStatus status,
                  void* userData);

struct DeviceManager
{
    DeviceManager()
    {
        auto errorCallback = [](RtAudioErrorType, const std::string& errorText)
        { throw std::runtime_error(errorText); };

        manager.setErrorCallback(errorCallback);
    }

    std::vector<DeviceInfo> getDevices()
    {
        std::vector<DeviceInfo> result;
        auto ids = manager.getDeviceIds();

        result.reserve(ids.size());

        for (auto id: ids)
            result.emplace_back(getInfo(manager.getDeviceInfo(id)));

        return result;
    }

    DeviceInfo getDefaultInputDevice()
    {
        return getInfo(manager.getDeviceInfo(manager.getDefaultInputDevice()));
    }

    DeviceInfo getDefaultOutputDevice()
    {
        return getInfo(manager.getDeviceInfo(manager.getDefaultOutputDevice()));
    }

    void start() { startStream(); }

    void stop()
    {
        if (isStreamRunning())
            stopStream();

        if (isStreamOpen())
            closeStream();
    }

    unsigned int openStream(const StreamConfig& configToUse)
    {
        config = configToUse;
        auto in = optionalToPointer<RtAudio::StreamParameters>(config.input,
                                                               getStreamParams);
        auto out = optionalToPointer<RtAudio::StreamParameters>(config.output,
                                                                getStreamParams);

        auto format = getFormat(config.format);
        auto& frames = config.maxBlockSize;
        auto options =
            optionalToPointer<RtAudio::StreamOptions>(config.options, getOptions);

        auto error = manager.openStream(out.get(),
                                        in.get(),
                                        format,
                                        config.sampleRate,
                                        &frames,
                                        audioCallback,
                                        this,
                                        options.get());

        auto e = getError(error);

        if (e != Error::NO_ERROR)
            throw std::runtime_error(manager.getErrorText());

        return frames;
    }

    long getStreamLatency() { return manager.getStreamLatency(); }
    unsigned int getStreamSampleRate() { return manager.getStreamSampleRate(); }

    AudioCallbackInfo prevInfo;
    Callback callback;
    StreamConfig config;

private:
    void closeStream() { manager.closeStream(); }
    void startStream() { manager.startStream(); }
    void stopStream() { manager.stopStream(); };
    void abortStream() { manager.abortStream(); }
    std::string getErrorText() { return manager.getErrorText(); }
    double getStreamTime() { return manager.getStreamTime(); }
    void setStreamTime(double time) { manager.setStreamTime(time); }
    bool isStreamOpen() const { return manager.isStreamOpen(); }
    bool isStreamRunning() const { return manager.isStreamRunning(); }
    void showWarnings(bool value) { manager.showWarnings(value); }

    RtAudio manager;
};

inline int audioCallback(void* outputBuffer,
                         void* inputBuffer,
                         unsigned int nFrames,
                         double streamTime,
                         RtAudioStreamStatus status,
                         void* userData)
{
    auto& manager = *static_cast<DeviceManager*>(userData);
    auto sr = manager.getStreamSampleRate();
    auto latency = manager.getStreamLatency();

    auto info = getCallbackInfo(outputBuffer,
                                inputBuffer,
                                nFrames,
                                streamTime,
                                status,
                                sr,
                                latency,
                                manager.config);

    if (manager.prevInfo != info)
    {
        manager.prevInfo = info;
        info.dirty = true;
    }

    manager.callback(info);

    return info.errorCode;
}

} // namespace MakeASound::RTAudio
