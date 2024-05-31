#pragma once

#include <RtAudio.h>

namespace MakeASound
{

template <typename BitType>
bool bitCompare(BitType bits, BitType bit)
{
    return bits & bit;
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

using Formats = std::vector<Format>;

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

struct DeviceInfo
{
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
    Formats nativeFormats;
};

DeviceInfo getInfo(const RtAudio::DeviceInfo& info)
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

unsigned int getDefaultNumChannels(const DeviceInfo& info, bool input)
{
    auto channels = info.outputChannels;

    if (input)
        channels = info.inputChannels;

    return std::min(unsigned(2), channels);
}

struct StreamParameters
{
    StreamParameters() = default;
    StreamParameters(const DeviceInfo& info, bool input)
        : deviceId(info.id)
        , nChannels(getDefaultNumChannels(info, input))
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
    bool nonInterleaved = true;
    bool minimizeLatency = true;
    bool hogDevice = false;
    bool scheduleRealTime = true;
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

struct AudioCallbackInfo
{
    template <typename T>
    const float* getInput(size_t channel) const
    {
        auto p = static_cast<T*>(inputBuffer);
        return &p[channel * nFrames];
    }

    template <typename T>
    float* getOutput(size_t channel)
    {
        auto p = static_cast<T*>(outputBuffer);
        return &p[channel * nFrames];
    }

    unsigned int numInputs = 0;
    unsigned int numOutputs = 0;
    void* outputBuffer = nullptr;
    void* inputBuffer = nullptr;
    unsigned int nFrames {};
    double streamTime {};
    RtAudioStreamStatus status {};
    int errorCode = 0;
};

using Callback = std::function<void(AudioCallbackInfo&)>;

inline unsigned int getNumChannels(const std::optional<StreamParameters>& params)
{
    if (params)
        return params->nChannels;

    return 0;
}

struct StreamConfig
{
    unsigned int getInputChannels() const { return getNumChannels(input); }
    unsigned int getOutputChannels() const { return getNumChannels(output); }

    std::optional<StreamParameters> input;
    std::optional<StreamParameters> output;
    Format format = Format::Float32;
    unsigned int sampleRate = {};
    unsigned int bufferFrames = 0;
    Callback callback = [](AudioCallbackInfo&) {};
    std::optional<StreamOptions> options;
};
} // namespace MakeASound