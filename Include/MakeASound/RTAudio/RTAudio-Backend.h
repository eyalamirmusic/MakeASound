#pragma once

#include <RtAudio.h>
#include "../DeviceInfo.h"

namespace MakeASound
{

template <typename BitType>
bool bitCompare(BitType bits, BitType bit)
{
    return bits & bit;
}

inline RtAudioFormat getFormat(Format format)
{
    switch (format)
    {
        case Format::Int8:
            return RTAUDIO_SINT8;
        case Format::Int16:
            return RTAUDIO_SINT16;
        case Format::Int24:
            return RTAUDIO_SINT24;
        case Format::Int32:
            return RTAUDIO_SINT32;
        case Format::Float32:
            return RTAUDIO_FLOAT32;
        case Format::Float64:
            return RTAUDIO_FLOAT64;
    }

    return RTAUDIO_FLOAT32;
}

inline void addFormat(Formats& formats, RtAudioFormat bits, Format format)
{
    auto bit = getFormat(format);

    if (bitCompare(bits, bit))
        formats.emplace_back(format);
}

inline Formats getFormats(RtAudioFormat formats)
{
    Formats result {};

    addFormat(result, formats, Format::Int8);
    addFormat(result, formats, Format::Int16);
    addFormat(result, formats, Format::Int24);
    addFormat(result, formats, Format::Int32);
    addFormat(result, formats, Format::Float32);
    addFormat(result, formats, Format::Float64);

    return result;
}

inline DeviceInfo getInfo(const RtAudio::DeviceInfo& info)
{
    DeviceInfo result;
    result.id = info.ID;
    result.name = info.name;
    result.outputChannels = info.outputChannels;
    result.inputChannels = info.inputChannels;
    result.duplexChannels = info.duplexChannels;
    result.isDefaultInput = info.isDefaultInput;
    result.isDefaultOutput = info.isDefaultOutput;
    result.sampleRates = info.sampleRates;
    result.currentSampleRate = info.currentSampleRate;
    result.preferredSampleRate = info.preferredSampleRate;
    result.nativeFormats = getFormats(info.nativeFormats);

    return result;
}

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

template <typename A, typename T, typename Func>
std::unique_ptr<A> optionalToPointer(const std::optional<T>& val, Func func)
{
    if (val.has_value())
        return std::make_unique<A>(func(val.value()));

    return nullptr;
}

inline RtAudio::StreamParameters getStreamParams(const StreamParameters& params)
{
    RtAudio::StreamParameters result;

    result.deviceId = params.deviceId;
    result.firstChannel = params.firstChannel;
    result.nChannels = params.nChannels;

    return result;
}

inline RtAudioStreamFlags getFlags(Flags flags)
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

inline RtAudio::StreamOptions getOptions(const StreamOptions& options)
{
    RtAudio::StreamOptions result;

    result.flags = getFlags(options.flags);
    result.priority = options.priority;
    result.numberOfBuffers = options.numberOfBuffers;
    result.streamName = options.streamName;

    return result;
}

inline AudioCallbackStatus getStatus(RtAudioStreamStatus status)
{
    if (status == RTAUDIO_OUTPUT_UNDERFLOW)
        return AudioCallbackStatus::OutputUnderflow;

    if (status == RTAUDIO_INPUT_OVERFLOW)
        return AudioCallbackStatus::InputOverflow;

    return AudioCallbackStatus::OK;
}

inline AudioCallbackInfo getCallbackInfo(void* outputBuffer,
                                         void* inputBuffer,
                                         unsigned int numSamples,
                                         double streamTime,
                                         RtAudioStreamStatus status,
                                         unsigned int sampleRate,
                                         unsigned int latency,
                                         const StreamConfig& config)
{
    AudioCallbackInfo info;

    info.inputBuffer = inputBuffer;
    info.outputBuffer = outputBuffer;
    info.numSamples = numSamples;
    info.streamTime = streamTime;
    info.status = getStatus(status);
    info.numInputs = config.getInputChannels();
    info.numOutputs = config.getOutputChannels();
    info.config = &config;
    info.sampleRate = sampleRate;
    info.latency = latency;
    info.maxBlockSize = numSamples;

    return info;
}

} // namespace MakeASound