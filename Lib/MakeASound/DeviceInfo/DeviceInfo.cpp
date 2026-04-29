#include "DeviceInfo.h"

#include <algorithm>

namespace MakeASound
{

int getDefaultNumChannels(const DeviceInfo& info, bool input)
{
    auto channels = info.outputChannels;

    if (input)
        channels = info.inputChannels;

    return std::min(2, channels);
}

bool deviceSupportsSampleRate(const DeviceInfo& device, int rate)
{
    return std::ranges::find(device.sampleRates, rate) != device.sampleRates.end();
}

int pickCompatibleSampleRate(const DeviceInfo& output, const DeviceInfo& input)
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

StreamParameters::StreamParameters(const DeviceInfo& deviceToUse,
                                   bool input,
                                   int numChannels)
    : device(deviceToUse)
{
    if (numChannels >= 0)
        nChannels = numChannels;
    else
        nChannels = getDefaultNumChannels(deviceToUse, input);
}

int getNumChannels(const std::optional<StreamParameters>& params)
{
    if (params)
        return params->nChannels;

    return 0;
}

int StreamConfig::getInputChannels() const { return getNumChannels(input); }
int StreamConfig::getOutputChannels() const { return getNumChannels(output); }

std::span<const float> AudioCallbackInfo::getInput(int channel) const
{
    return {&inputBuffer[channel * numSamples], static_cast<size_t>(numSamples)};
}

std::span<float> AudioCallbackInfo::getOutput(int channel)
{
    return {&outputBuffer[channel * numSamples], static_cast<size_t>(numSamples)};
}

std::span<const float> AudioCallbackInfo::getInterleavedInputs() const
{
    return {inputBuffer, static_cast<size_t>(numInputs * numSamples)};
}

std::span<float> AudioCallbackInfo::getInterleavedOutputs()
{
    return {outputBuffer, static_cast<size_t>(numOutputs * numSamples)};
}

bool AudioCallbackInfo::operator==(const AudioCallbackInfo& other) const
{
    return numInputs == other.numInputs && numOutputs == other.numOutputs
           && sampleRate == other.sampleRate
           && maxBlockSize == other.maxBlockSize;
}

bool AudioCallbackInfo::operator!=(const AudioCallbackInfo& other) const
{
    return !operator==(other);
}

} // namespace MakeASound
