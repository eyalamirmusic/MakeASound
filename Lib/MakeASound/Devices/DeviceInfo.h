#pragma once

#include <Miro/Miro.h>

#include "../Common/Common.h"
#include "../Audio/Buffer.h"

#include <optional>
#include <string>
#include <functional>

namespace MakeASound
{

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
                 preferredSampleRate)

    int id {};
    std::string name;
    int outputChannels {};
    int inputChannels {};
    int duplexChannels {};
    bool isDefaultOutput {false};
    bool isDefaultInput {false};
    Vector<int> sampleRates;
    int currentSampleRate {};
    int preferredSampleRate {};
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

int getDefaultNumChannels(const DeviceInfo& info, bool input);
bool deviceSupportsSampleRate(const DeviceInfo& device, int rate);

// Pick a sample rate both devices can drive. Prefers the output's preferred
// rate, then the input's preferred rate, then the highest rate present in
// both lists, with output-only fallbacks if no common rate exists.
int pickCompatibleSampleRate(const DeviceInfo& output, const DeviceInfo& input);

struct StreamParameters
{
    StreamParameters() = default;
    StreamParameters(const DeviceInfo& deviceToUse,
                     bool input,
                     int numChannels = -1,
                     int firstCh = 0);

    MIRO_REFLECT(device, nChannels, firstChannel)

    DeviceInfo device;

    // A stream uses the contiguous span of a device's channels starting at
    // firstChannel: [firstChannel, firstChannel + nChannels). This is how a
    // specific input/output (e.g. a stereo pair or a single channel) is picked
    // out of a multi-channel device such as an audio interface.
    int nChannels {};
    int firstChannel {};
};

struct Flags
{
    MIRO_REFLECT(nonInterleaved, minimizeLatency, hogDevice)

    bool nonInterleaved = true;
    bool minimizeLatency = false;
    bool hogDevice = false;
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

int getNumChannels(const std::optional<StreamParameters>& params);

struct StreamConfig
{
    int getInputChannels() const;
    int getOutputChannels() const;

    MIRO_REFLECT(input, output, sampleRate, maxBlockSize, options)

    std::optional<StreamParameters> input;
    std::optional<StreamParameters> output;

    int sampleRate {};
    int maxBlockSize = 0;
    std::optional<StreamOptions> options;
};

struct AudioCallbackInfo
{
    // Planar (non-interleaved) views over this block's channels. The backend
    // owns the interleaved<->planar conversion, so callers only ever see planar
    // data through these.
    Buffer getInput() const;
    Buffer getOutput();

    bool operator==(const AudioCallbackInfo& other) const;
    bool operator!=(const AudioCallbackInfo& other) const;

    int numInputs = 0;
    int numOutputs = 0;
    float* outputBuffer = nullptr;
    float* inputBuffer = nullptr;
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
