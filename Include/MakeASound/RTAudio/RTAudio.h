#pragma once

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
    StreamParameters() = default;
    StreamParameters(const DeviceInfo& info, bool input)
        : deviceId(info.id)
        , nChannels(input ? info.inputChannels : info.outputChannels)
    {
    }

    unsigned int deviceId {};
    unsigned int nChannels {};
    unsigned int firstChannel {};
};

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

struct Flags
{
    bool nonInterleaved = false;
    bool minimizeLatency = false;
    bool hogDevice = false;
    bool scheduleRealTime = false;
    bool alsaUseDefault = false;
    bool jackDontConnect = false;
};

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

struct StreamOptions
{
    Flags flags {};
    unsigned int numberOfBuffers {};
    std::string streamName {};
    int priority {};
};

inline RtAudio::StreamOptions getOptions(const StreamOptions& options)
{
    RtAudio::StreamOptions result;

    result.flags = getFlags(options.flags);
    result.priority = options.priority;
    result.numberOfBuffers = options.numberOfBuffers;
    result.streamName = options.streamName;

    return result;
}

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
    std::optional<StreamParameters> input;
    std::optional<StreamParameters> output;
    Format format = Format::Float32;
    unsigned int sampleRate = {};
    unsigned int bufferFrames = 0;
    RtAudioCallback callback {};
    void* userData = nullptr;
    std::optional<StreamOptions> options;
};

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
} // namespace MakeASound