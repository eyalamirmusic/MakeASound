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

std::vector<DeviceInfo> DeviceManager::getDevices()
{
    std::vector<DeviceInfo> result;
    auto ids = manager.getDeviceIds();

    result.reserve(ids.size());

    for (auto id: ids)
        result.emplace_back(getInfo(manager.getDeviceInfo(id)));

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

unsigned int DeviceManager::openStream(const StreamConfig& configToUse)
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

    manager.openStream(out.get(),
                       in.get(),
                       format,
                       config.sampleRate,
                       &frames,
                       audioCallback,
                       this,
                       options.get());

    return frames;
}

long DeviceManager::getStreamLatency()
{
    return manager.getStreamLatency();
}

unsigned int DeviceManager::getStreamSampleRate()
{
    return manager.getStreamSampleRate();
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


    manager.callback(info);

    return info.errorCode;
}

} // namespace MakeASound::RTAudio
