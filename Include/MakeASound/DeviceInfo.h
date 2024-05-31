#pragma once

#include <vector>

namespace MakeASound
{

enum class Format
{
    Int8,
    Int16,
    Int24,
    Int32,
    Float32,
    Float64
};

using Formats = std::vector<Format>;

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

inline unsigned int getDefaultNumChannels(const DeviceInfo& info, bool input)
{
    auto channels = info.outputChannels;

    if (input)
        channels = info.inputChannels;

    return std::min(static_cast<unsigned>(2), channels);
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

struct Flags
{
    bool nonInterleaved = true;
    bool minimizeLatency = true;
    bool hogDevice = false;
    bool scheduleRealTime = true;
    bool alsaUseDefault = false;
    bool jackDontConnect = false;
};

struct StreamOptions
{
    Flags flags {};
    unsigned int numberOfBuffers {};
    std::string streamName {};
    int priority {};
};

enum class AudioCallbackStatus
{
    OK,
    InputOverflow,
    OutputUnderflow
};

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
    AudioCallbackStatus status = AudioCallbackStatus::OK;
    int errorCode = 0;
};

inline unsigned int getNumChannels(const std::optional<StreamParameters>& params)
{
    if (params)
        return params->nChannels;

    return 0;
}

using Callback = std::function<void(AudioCallbackInfo&)>;

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