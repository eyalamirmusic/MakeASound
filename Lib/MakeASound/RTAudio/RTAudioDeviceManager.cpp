#include "RTAudioDeviceManager.h"

#include <stdexcept>

namespace MakeASound::RTAudio
{

DeviceManager::DeviceManager()
{
    auto errorCallback = [](RtAudioErrorType type, const std::string& errorText)
    {
        if (type == RTAUDIO_WARNING || type == RTAUDIO_NO_ERROR)
            return;

        throw std::runtime_error(errorText);
    };

    manager.setErrorCallback(errorCallback);
}

Vector<DeviceInfo> DeviceManager::getDevices()
{
    auto result = Vector<DeviceInfo> {};
    auto ids = manager.getDeviceIds();

    result.reserve(static_cast<int>(ids.size()));

    for (auto id: ids)
        result.add(getInfo(manager.getDeviceInfo(id)));

    return result;
}

DeviceInfo DeviceManager::getDefaultInputDevice()
{
    return getInfo(manager.getDeviceInfo(manager.getDefaultInputDevice()));
}

DeviceInfo DeviceManager::getDefaultOutputDevice()
{
    return getInfo(manager.getDeviceInfo(manager.getDefaultOutputDevice()));
}

void DeviceManager::start()
{
    startStream();
}

void DeviceManager::stop()
{
    if (isStreamRunning())
        stopStream();

    if (isStreamOpen())
        closeStream();
}

int DeviceManager::openStream(const StreamConfig& configToUse)
{
    config = configToUse;
    auto in = optionalToPointer<RtAudio::StreamParameters>(config.input,
                                                           getStreamParams);
    auto out = optionalToPointer<RtAudio::StreamParameters>(config.output,
                                                            getStreamParams);

    auto format = getFormat(config.format);
    auto frames = static_cast<unsigned int>(config.maxBlockSize);
    auto options =
        optionalToPointer<RtAudio::StreamOptions>(config.options, getOptions);

    manager.openStream(out.get(),
                       in.get(),
                       format,
                       static_cast<unsigned int>(config.sampleRate),
                       &frames,
                       audioCallback,
                       this,
                       options.get());

    cachedSampleRate = manager.getStreamSampleRate();
    cachedLatency = manager.getStreamLatency();
    config.maxBlockSize = static_cast<int>(frames);

    return static_cast<int>(frames);
}

long DeviceManager::getStreamLatency()
{
    return manager.getStreamLatency();
}

int DeviceManager::getStreamSampleRate()
{
    return static_cast<int>(manager.getStreamSampleRate());
}

void DeviceManager::closeStream() { manager.closeStream(); }
void DeviceManager::startStream() { manager.startStream(); }
void DeviceManager::stopStream() { manager.stopStream(); }
void DeviceManager::abortStream() { manager.abortStream(); }
std::string DeviceManager::getErrorText() { return manager.getErrorText(); }
double DeviceManager::getStreamTime() { return manager.getStreamTime(); }
void DeviceManager::setStreamTime(double time) { manager.setStreamTime(time); }
bool DeviceManager::isStreamOpen() const { return manager.isStreamOpen(); }
bool DeviceManager::isStreamRunning() const { return manager.isStreamRunning(); }
void DeviceManager::showWarnings(bool value) { manager.showWarnings(value); }

int audioCallback(void* outputBuffer,
                  void* inputBuffer,
                  unsigned int nFrames,
                  double streamTime,
                  RtAudioStreamStatus status,
                  void* userData)
{
    auto& manager = *static_cast<DeviceManager*>(userData);

    auto info = getCallbackInfo(outputBuffer,
                                inputBuffer,
                                nFrames,
                                streamTime,
                                status,
                                manager.cachedSampleRate,
                                static_cast<unsigned int>(manager.cachedLatency),
                                manager.config);


    manager.callback(info);

    return info.errorCode;
}

} // namespace MakeASound::RTAudio
