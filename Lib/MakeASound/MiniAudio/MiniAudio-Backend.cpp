#include "MiniAudio-Backend.h"

namespace MakeASound::MiniAudio
{

Error getError(ma_result result)
{
    switch (result)
    {
        case MA_SUCCESS:
            return Error::NoError;
        case MA_NO_DEVICE:
        case MA_DOES_NOT_EXIST:
            return Error::NO_DEVICES_FOUND;
        case MA_INVALID_DEVICE_CONFIG:
        case MA_DEVICE_NOT_INITIALIZED:
        case MA_DEVICE_ALREADY_INITIALIZED:
        case MA_DEVICE_NOT_STARTED:
        case MA_DEVICE_NOT_STOPPED:
            return Error::INVALID_DEVICE;
        case MA_INVALID_ARGS:
            return Error::INVALID_PARAMETER;
        case MA_INVALID_OPERATION:
            return Error::INVALID_USE;
        case MA_OUT_OF_MEMORY:
            return Error::MEMORY_ERROR;
        case MA_DEVICE_TYPE_NOT_SUPPORTED:
        case MA_BACKEND_NOT_ENABLED:
            return Error::DRIVER_ERROR;
        case MA_FAILED_TO_OPEN_BACKEND_DEVICE:
        case MA_FAILED_TO_INIT_BACKEND:
            return Error::SYSTEM_ERROR;
        case MA_FAILED_TO_START_BACKEND_DEVICE:
        case MA_FAILED_TO_STOP_BACKEND_DEVICE:
            return Error::THREAD_ERROR;
        default:
            return Error::UNKNOWN_ERROR;
    }
}

AudioCallbackStatus getStatus(ma_result result)
{
    if (result == MA_SUCCESS)
        return AudioCallbackStatus::OK;

    return AudioCallbackStatus::OutputUnderflow;
}

Vector<int> collectSampleRates(const ma_device_info& info)
{
    static constexpr int standardRates[] = {
        8000, 11025, 16000, 22050, 24000, 32000,
        44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};

    auto rates = Vector<int> {};

    for (auto i = 0u; i < info.nativeDataFormatCount; ++i)
    {
        auto rate = static_cast<int>(info.nativeDataFormats[i].sampleRate);

        if (rate == 0)
            for (auto standard: standardRates)
                rates.addIfNotThere(standard);
        else
            rates.addIfNotThere(rate);
    }

    rates.sort();
    return rates;
}

int pickPreferredSampleRate(const Vector<int>& rates)
{
    if (rates.contains(48000))
        return 48000;

    if (rates.contains(44100))
        return 44100;

    if (!rates.empty())
        return rates.front();

    return 0;
}

} // namespace MakeASound::MiniAudio
