#pragma once

#include "RtAudio/RtAudio.h"

namespace MakeASound
{
struct DeviceManager
{
    DeviceManager()
    {
        auto errorCallback = [](RtAudioErrorType, const std::string& errorText)
        { throw std::runtime_error(errorText); };

        manager.setErrorCallback(errorCallback);
    }
    unsigned int getDeviceCount() { return manager.getDeviceCount(); }

    std::vector<std::string> getDeviceNames() { return manager.getDeviceNames(); };

    std::vector<DeviceInfo> getDevices()
    {
        std::vector<DeviceInfo> result;
        auto ids = manager.getDeviceIds();

        result.reserve(ids.size());

        for (auto id: ids)
            result.emplace_back(manager.getDeviceInfo(id));

        return result;
    }

    DeviceInfo getDefaultInputDevice()
    {
        return manager.getDeviceInfo(manager.getDefaultInputDevice());
    }

    DeviceInfo getDefaultOutputDevice()
    {
        return manager.getDeviceInfo(manager.getDefaultOutputDevice());
    }

    StreamConfig getDefaultConfig()
    {
        StreamConfig config;

        config.input = StreamParameters(getDefaultInputDevice(), true);
        config.output = StreamParameters(getDefaultInputDevice(), false);

        config.sampleRate = 44100;
        config.bufferFrames = 512;

        return config;
    }

    unsigned int openStream(const StreamConfig& config)
    {
        auto in = optionalToPointer<RtAudio::StreamParameters>(config.input,
                                                               getStreamParams);
        auto out = optionalToPointer<RtAudio::StreamParameters>(config.output,
                                                                getStreamParams);

        auto format = getFormat(config.format);
        auto frames = config.bufferFrames;

        auto options =
            optionalToPointer<RtAudio::StreamOptions>(config.options, getOptions);

        auto error = manager.openStream(out.get(),
                                        in.get(),
                                        format,
                                        config.sampleRate,
                                        &frames,
                                        config.callback,
                                        config.userData,
                                        options.get());

        auto e = getError(error);

        if (e != Error::NO_ERROR)
            throw std::runtime_error(manager.getErrorText());

        return frames;
    }

    void closeStream() { manager.closeStream(); }
    void startStream() { manager.startStream(); }
    void stopStream() { manager.stopStream(); };
    void abortStream() { manager.abortStream(); }
    std::string getErrorText() { return manager.getErrorText(); }
    long getStreamLatency() { return manager.getStreamLatency(); }
    unsigned int getStreamSampleRate() { return manager.getStreamSampleRate(); }
    double getStreamTime() { return manager.getStreamTime(); }
    void setStreamTime(double time) { manager.setStreamTime(time); }
    bool isStreamOpen() const { return manager.isStreamOpen(); }
    bool isStreamRunning() const { return manager.isStreamRunning(); }
    void showWarnings(bool value) { manager.showWarnings(value); }

private:
    RtAudio manager;
};

} // namespace MakeASound
