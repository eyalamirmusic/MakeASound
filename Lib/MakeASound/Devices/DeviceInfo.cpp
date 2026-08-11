#include "DeviceInfo.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace MakeASound
{
namespace
{
struct BackendName
{
    Backend backend;
    const char* label;
};

// The display spelling of every API. Order is only cosmetic — what a manager can
// actually reach is DeviceManager::getAvailableBackends.
constexpr auto backendNames =
    std::array {BackendName {Backend::WASAPI, "WASAPI"},
                BackendName {Backend::DirectSound, "DirectSound"},
                BackendName {Backend::WinMM, "WinMM"},
                BackendName {Backend::CoreAudio, "Core Audio"},
                BackendName {Backend::SndIO, "sndio"},
                BackendName {Backend::Audio4, "audio(4)"},
                BackendName {Backend::OSS, "OSS"},
                BackendName {Backend::PulseAudio, "PulseAudio"},
                BackendName {Backend::ALSA, "ALSA"},
                BackendName {Backend::JACK, "JACK"},
                BackendName {Backend::AAudio, "AAudio"},
                BackendName {Backend::OpenSL, "OpenSL|ES"},
                BackendName {Backend::WebAudio, "Web Audio"},
                BackendName {Backend::Null, "Null"}};

// Everything a user might type between the words of a driver name, dropped so the
// comparison is against the letters alone.
std::string squash(std::string_view text)
{
    auto result = std::string {};

    for (auto c: text)
        if (std::isalnum(static_cast<unsigned char>(c)) != 0)
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    return result;
}
} // namespace

std::string getBackendName(Backend backend)
{
    for (const auto& entry: backendNames)
        if (entry.backend == backend)
            return entry.label;

    return {};
}

std::optional<Backend> getBackendFromName(std::string_view name)
{
    auto wanted = squash(name);

    if (wanted.empty())
        return std::nullopt;

    for (const auto& entry: backendNames)
    {
        // Both spellings: the display one above and the enumerator's, so a name
        // written down from a log or a JSON dump comes back too.
        if (squash(entry.label) == wanted
            || squash(Miro::enumToString(entry.backend)) == wanted)
            return entry.backend;
    }

    return std::nullopt;
}

bool DeviceInfo::isValid() const
{
    return outputChannels > 0 || inputChannels > 0;
}

bool DeviceInfo::hasChannels(bool input) const
{
    return (input ? inputChannels : outputChannels) > 0;
}

std::string getErrorMessage(Error error)
{
    switch (error)
    {
        case Error::NoError:
            return {};
        case Error::NO_DEVICES_FOUND:
            return "No audio device is available";
        case Error::INVALID_DEVICE:
            return "The audio device is no longer usable";
        case Error::DEVICE_DISCONNECT:
            return "The audio device was disconnected";
        case Error::INVALID_PARAMETER:
            return "The audio device does not support these settings";
        case Error::INVALID_USE:
            return "The audio stream was not set up correctly";
        case Error::MEMORY_ERROR:
            return "Ran out of memory opening the audio device";
        case Error::DRIVER_ERROR:
            return "The audio driver does not support this stream";
        case Error::SYSTEM_ERROR:
            return "The system audio service is unavailable";
        case Error::THREAD_ERROR:
            return "The audio device could not be started";
        case Error::WARNING:
        case Error::UNKNOWN_ERROR:
        default:
            return "The audio device could not be opened";
    }
}

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

    // No output to speak of — an input-only machine, or one where the output side was
    // never filled in. The input's own rates are the only ones left worth asking for.
    if (input.preferredSampleRate > 0)
        return input.preferredSampleRate;

    if (!input.sampleRates.empty())
        return input.sampleRates.front();

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
    return {inputBuffer, numInputs, numSamples};
}

Buffer AudioCallbackInfo::getOutput()
{
    return {outputBuffer, numOutputs, numSamples};
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
