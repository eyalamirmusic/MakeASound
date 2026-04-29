#pragma once

#include <Miro/Miro.h>

#include <algorithm>
#include <vector>
#include <optional>
#include <string>
#include <functional>

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
    MIRO_REFLECT(id,
                 name,
                 outputChannels,
                 inputChannels,
                 duplexChannels,
                 isDefaultOutput,
                 isDefaultInput,
                 sampleRates,
                 currentSampleRate,
                 preferredSampleRate,
                 nativeFormats)

    int id {};
    std::string name;
    int outputChannels {};
    int inputChannels {};
    int duplexChannels {};
    bool isDefaultOutput {false};
    bool isDefaultInput {false};
    std::vector<int> sampleRates;
    int currentSampleRate {};
    int preferredSampleRate {};
    Formats nativeFormats;
};

enum class Error
{
    NoError,
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

inline int getDefaultNumChannels(const DeviceInfo& info, bool input)
{
    auto channels = info.outputChannels;

    if (input)
        channels = info.inputChannels;

    return std::min(2, channels);
}

inline bool deviceSupportsSampleRate(const DeviceInfo& device, int rate)
{
    return std::ranges::find(device.sampleRates, rate) != device.sampleRates.end();
}

// Pick a sample rate both devices can drive. Prefers the output's preferred
// rate, then the input's preferred rate, then the highest rate present in
// both lists, with output-only fallbacks if no common rate exists.
inline int pickCompatibleSampleRate(const DeviceInfo& output,
                                    const DeviceInfo& input)
{
    auto isCommon = [&](int rate)
    {
        return deviceSupportsSampleRate(output, rate)
               && deviceSupportsSampleRate(input, rate);
    };

    if (output.preferredSampleRate > 0 && isCommon(output.preferredSampleRate))
        return output.preferredSampleRate;

    if (input.preferredSampleRate > 0 && isCommon(input.preferredSampleRate))
        return input.preferredSampleRate;

    auto best = 0;
    for (auto rate: output.sampleRates)
        if (rate > best && isCommon(rate))
            best = rate;

    if (best > 0)
        return best;

    if (output.preferredSampleRate > 0)
        return output.preferredSampleRate;

    if (!output.sampleRates.empty())
        return output.sampleRates.front();

    return 44100;
}

struct StreamParameters
{
    StreamParameters() = default;
    StreamParameters(const DeviceInfo& deviceToUse, bool input, int numChannels = -1)
        : device(deviceToUse)
    {
        if (numChannels >= 0)
            nChannels = numChannels;
        else
            nChannels = getDefaultNumChannels(deviceToUse, input);
    }

    MIRO_REFLECT(device, nChannels, firstChannel)

    DeviceInfo device;

    int nChannels {};
    int firstChannel {};
};

struct Flags
{
    MIRO_REFLECT(nonInterleaved,
                 minimizeLatency,
                 hogDevice,
                 scheduleRealTime,
                 alsaUseDefault,
                 jackDontConnect)

    bool nonInterleaved = true;
    bool minimizeLatency = true;
    bool hogDevice = false;
    bool scheduleRealTime = false;
    bool alsaUseDefault = false;
    bool jackDontConnect = false;
};

struct StreamOptions
{
    MIRO_REFLECT(flags, numberOfBuffers, streamName, priority)

    Flags flags {};
    int numberOfBuffers {};
    std::string streamName {};
    int priority {};
};

enum class AudioCallbackStatus
{
    OK,
    InputOverflow,
    OutputUnderflow
};

inline int getNumChannels(const std::optional<StreamParameters>& params)
{
    if (params)
        return params->nChannels;

    return 0;
}

struct StreamConfig
{
    int getInputChannels() const { return getNumChannels(input); }
    int getOutputChannels() const { return getNumChannels(output); }

    MIRO_REFLECT(input, output, format, sampleRate, maxBlockSize, options)

    std::optional<StreamParameters> input;
    std::optional<StreamParameters> output;
    Format format = Format::Float32;

    int sampleRate {};
    int maxBlockSize = 0;
    std::optional<StreamOptions> options;
};

struct AudioCallbackInfo
{
    template <typename T>
    const T* getInput(int channel) const
    {
        auto p = static_cast<T*>(inputBuffer);
        return &p[channel * numSamples];
    }

    template <typename T>
    T* getOutput(int channel)
    {
        auto p = static_cast<T*>(outputBuffer);
        return &p[channel * numSamples];
    }

    template <typename T>
    const T* getInterleavedInputs() const
    {
        return static_cast<T*>(inputBuffer);
    }

    template <typename T>
    const T* getInterleavedOutputs() const
    {
        return static_cast<T*>(outputBuffer);
    }

    bool operator==(const AudioCallbackInfo& other) const
    {
        return numInputs == other.numInputs && numOutputs == other.numOutputs
               && sampleRate == other.sampleRate
               && maxBlockSize == other.maxBlockSize;
    }

    bool operator!=(const AudioCallbackInfo& other) const
    {
        return !operator==(other);
    }

    int numInputs = 0;
    int numOutputs = 0;
    void* outputBuffer = nullptr;
    void* inputBuffer = nullptr;
    int numSamples {};
    double streamTime {};
    AudioCallbackStatus status = AudioCallbackStatus::OK;

    int sampleRate = 0;
    int maxBlockSize = 0;
    int latency = 0;

    bool dirty = false;
    int errorCode = 0;
};

using Callback = std::function<void(AudioCallbackInfo&)>;
} // namespace MakeASound
