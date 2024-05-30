#pragma once

#include <RtAudio.h>

namespace MakeASound
{

struct DeviceInfo
{
    DeviceInfo() = default;
    DeviceInfo(const RtAudio::DeviceInfo& info)
    {
        id = info.ID;
        name = info.name;
        outputChannels = info.outputChannels;
        inputChannels = info.inputChannels;
        duplexChannels = info.duplexChannels;
        isDefaultInput = info.isDefaultInput;
        isDefaultOutput = info.isDefaultOutput;
        sampleRates = info.sampleRates;
        currentSampleRate = info.currentSampleRate;
        preferredSampleRate = info.preferredSampleRate;
        nativeFormats = info.nativeFormats;
    }

    unsigned int id {};
    std::string name;
    unsigned int outputChannels {};
    unsigned int inputChannels {};
    unsigned int duplexChannels {};
    bool isDefaultOutput {false};
    bool isDefaultInput {false};
    std::vector<unsigned int> sampleRates;
    unsigned int currentSampleRate {};
    unsigned int preferredSampleRate {};
    RtAudioFormat nativeFormats {};
};

enum class Error
{
    NO_ERROR,
    WARNING,
    UNKNOWN_ERROR,
    NO_DEVICES_FOUND,
    INVALID_DEVICE,
    DEVICE_DISCONNECT,
    MEMORY_ERROR,
    INVALID_PARAMETER,
    INVALID_USE,
    DRIVER_ERROR,
    SYSTEM_ERROR,
    THREAD_ERROR
};

inline Error getError(RtAudioErrorType error)
{
    switch (error)
    {
        case RTAUDIO_NO_ERROR:
            return Error::NO_ERROR;
        case RTAUDIO_WARNING:
            return Error::WARNING;
        case RTAUDIO_UNKNOWN_ERROR:
            return Error::UNKNOWN_ERROR;
        case RTAUDIO_NO_DEVICES_FOUND:
            return Error::NO_DEVICES_FOUND;
        case RTAUDIO_INVALID_DEVICE:
            return Error::INVALID_DEVICE;
        case RTAUDIO_DEVICE_DISCONNECT:
            return Error::DEVICE_DISCONNECT;
        case RTAUDIO_MEMORY_ERROR:
            return Error::MEMORY_ERROR;
        case RTAUDIO_INVALID_PARAMETER:
            return Error::INVALID_PARAMETER;
        case RTAUDIO_INVALID_USE:
            return Error::INVALID_USE;
        case RTAUDIO_DRIVER_ERROR:
            return Error::DRIVER_ERROR;
        case RTAUDIO_SYSTEM_ERROR:
            return Error::SYSTEM_ERROR;
        case RTAUDIO_THREAD_ERROR:
            return Error::THREAD_ERROR;
    }

    return Error::NO_ERROR;
}

struct StreamParameters
{
    unsigned int deviceId {};
    unsigned int nChannels {};
    unsigned int firstChannel {};
};

struct Flags
{
    bool nonInterleaved = false;
    bool minimizeLatency = false;
    bool hogDevice = false;
    bool scheduleRealTime = false;
    bool alsaUseDefault = false;
    bool jackDontConnect = false;
};

struct StreamOptions
{
    unsigned int flags {};
    unsigned int numberOfBuffers {};
    std::string streamName;
    int priority {};
};

enum class Format
{
    Int8,
    Int16,
    Int24,
    Int32,
    Float32,
    Float64
};

struct StreamConfig
{
    RtAudio::StreamParameters outputParameters;

    RtAudio::StreamParameters inputParameters;
    Format format = Format::Float32;
    unsigned int sampleRate = {};
    unsigned int* bufferFrames = nullptr;
    RtAudioCallback callback {};
    void* userData = nullptr;
    RtAudio::StreamOptions* options = nullptr;
};

struct DeviceManager
{
    unsigned int getDeviceCount() { return manager.getDeviceCount(); }
    std::vector<unsigned int> getDeviceIds() { return manager.getDeviceIds(); }
    std::vector<std::string> getDeviceNames() { return manager.getDeviceNames(); };
    DeviceInfo getDeviceInfo(unsigned int deviceId)
    {
        return {manager.getDeviceInfo(deviceId)};
    }
    unsigned int getDefaultInputDevice() { return manager.getDefaultInputDevice(); }
    unsigned int getDefaultOutputDevice()
    {
        return manager.getDefaultOutputDevice();
    }
    Error openStream(const StreamConfig& /*config*/) { return Error::SYSTEM_ERROR; }
    void closeStream() { manager.closeStream(); }
    RtAudioErrorType startStream() { return manager.startStream(); }
    RtAudioErrorType stopStream() { return manager.stopStream(); };
    RtAudioErrorType abortStream() { return manager.abortStream(); }
    std::string getErrorText() { return manager.getErrorText(); }
    long getStreamLatency() { return manager.getStreamLatency(); }
    unsigned int getStreamSampleRate() { return manager.getStreamSampleRate(); }
    double getStreamTime() { return manager.getStreamTime(); }
    void setStreamTime(double time) { manager.setStreamTime(time); }
    bool isStreamOpen() const { return manager.isStreamOpen(); }
    bool isStreamRunning() const { return manager.isStreamRunning(); }
    void setErrorCallback(RtAudioErrorCallback errorCallback)
    {
        manager.setErrorCallback(errorCallback);
    }
    void showWarnings(bool value) { manager.showWarnings(value); }

    RtAudio manager;
};

} // namespace MakeASound
