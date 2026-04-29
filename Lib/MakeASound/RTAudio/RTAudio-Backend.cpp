#include "RTAudio-Backend.h"

namespace MakeASound::RTAudio
{
DeviceInfo getInfo(const RtAudio::DeviceInfo& info)
{
    DeviceInfo result;
    result.id = static_cast<int>(info.ID);
    result.name = info.name;
    result.outputChannels = static_cast<int>(info.outputChannels);
    result.inputChannels = static_cast<int>(info.inputChannels);
    result.duplexChannels = static_cast<int>(info.duplexChannels);
    result.isDefaultInput = info.isDefaultInput;
    result.isDefaultOutput = info.isDefaultOutput;

    result.sampleRates.reserve(info.sampleRates.size());
    for (auto rate: info.sampleRates)
        result.sampleRates.push_back(static_cast<int>(rate));

    result.currentSampleRate = static_cast<int>(info.currentSampleRate);
    result.preferredSampleRate = static_cast<int>(info.preferredSampleRate);

    return result;
}

Error getError(RtAudioErrorType error)
{
    switch (error)
    {
        case RTAUDIO_NO_ERROR:
            return Error::NoError;
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

    return Error::NoError;
}

RtAudio::StreamParameters getStreamParams(const StreamParameters& params)
{
    RtAudio::StreamParameters result;

    result.deviceId = static_cast<unsigned int>(params.device.id);
    result.firstChannel = static_cast<unsigned int>(params.firstChannel);
    result.nChannels = static_cast<unsigned int>(params.nChannels);

    return result;
}

RtAudioStreamFlags getFlags(Flags flags)
{
    RtAudioStreamFlags result {};

    if (flags.nonInterleaved)
        result |= RTAUDIO_NONINTERLEAVED;

    if (flags.minimizeLatency)
        result |= RTAUDIO_MINIMIZE_LATENCY;

    if (flags.hogDevice)
        result |= RTAUDIO_HOG_DEVICE;

    if (flags.scheduleRealTime)
        result |= RTAUDIO_SCHEDULE_REALTIME;

    if (flags.alsaUseDefault)
        result |= RTAUDIO_ALSA_USE_DEFAULT;

    if (flags.jackDontConnect)
        result |= RTAUDIO_JACK_DONT_CONNECT;

    return result;
}

RtAudio::StreamOptions getOptions(const StreamOptions& options)
{
    RtAudio::StreamOptions result;

    result.flags = getFlags(options.flags);
    result.priority = options.priority;
    result.numberOfBuffers = static_cast<unsigned int>(options.numberOfBuffers);
    result.streamName = options.streamName;

    return result;
}

AudioCallbackStatus getStatus(RtAudioStreamStatus status)
{
    if (status == RTAUDIO_OUTPUT_UNDERFLOW)
        return AudioCallbackStatus::OutputUnderflow;

    if (status == RTAUDIO_INPUT_OVERFLOW)
        return AudioCallbackStatus::InputOverflow;

    return AudioCallbackStatus::OK;
}

AudioCallbackInfo getCallbackInfo(void* outputBuffer,
                                  void* inputBuffer,
                                  unsigned int numSamples,
                                  double streamTime,
                                  RtAudioStreamStatus status,
                                  unsigned int sampleRate,
                                  unsigned int latency,
                                  const StreamConfig& config)
{
    AudioCallbackInfo info;

    info.inputBuffer = static_cast<float*>(inputBuffer);
    info.outputBuffer = static_cast<float*>(outputBuffer);
    info.numSamples = static_cast<int>(numSamples);
    info.streamTime = streamTime;
    info.status = getStatus(status);
    info.numInputs = config.getInputChannels();
    info.numOutputs = config.getOutputChannels();
    info.sampleRate = static_cast<int>(sampleRate);
    info.latency = static_cast<int>(latency);
    info.maxBlockSize = config.maxBlockSize;

    return info;
}

} // namespace MakeASound::RTAudio
