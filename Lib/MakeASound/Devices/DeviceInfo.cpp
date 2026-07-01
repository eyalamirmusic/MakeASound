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
                                   int numChannels,
                                   int firstCh)
    : device(deviceToUse)
    , firstChannel(firstCh)
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

Buffer AudioCallbackInfo::getInput() const
{
    auto data =
        std::span<float> {inputBuffer, static_cast<size_t>(numInputs * numSamples)};

    return {data, numInputs};
}

Buffer AudioCallbackInfo::getOutput()
{
    auto data =
        std::span<float> {outputBuffer, static_cast<size_t>(numOutputs * numSamples)};

    return {data, numOutputs};
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
