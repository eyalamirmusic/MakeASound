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
    DeviceManager();

    std::vector<DeviceInfo> getDevices();
    DeviceInfo getDefaultInputDevice();
    DeviceInfo getDefaultOutputDevice();

    void start();
    void stop();
    unsigned int openStream(const StreamConfig& configToUse);

    long getStreamLatency();
    unsigned int getStreamSampleRate();

    Callback callback;
    StreamConfig config;

private:
    void closeStream();
    void startStream();
    void stopStream();
    void abortStream();
    std::string getErrorText();
    double getStreamTime();
    void setStreamTime(double time);
    bool isStreamOpen() const;
    bool isStreamRunning() const;
    void showWarnings(bool value);

    RtAudio manager;
};

} // namespace MakeASound::RTAudio
