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
        case MA_NO_BACKEND:
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

DeviceNotification getNotification(ma_device_notification_type type)
{
    switch (type)
    {
        case ma_device_notification_type_started:
            return DeviceNotification::Started;
        case ma_device_notification_type_rerouted:
            return DeviceNotification::Rerouted;
        case ma_device_notification_type_interruption_began:
            return DeviceNotification::InterruptionBegan;
        case ma_device_notification_type_interruption_ended:
            return DeviceNotification::InterruptionEnded;
        case ma_device_notification_type_unlocked:
            return DeviceNotification::Unlocked;
        case ma_device_notification_type_stopped:
        default:
            return DeviceNotification::Stopped;
    }
}

Backend getBackend(ma_backend backend)
{
    switch (backend)
    {
        case ma_backend_wasapi:
            return Backend::WASAPI;
        case ma_backend_dsound:
            return Backend::DirectSound;
        case ma_backend_winmm:
            return Backend::WinMM;
        case ma_backend_coreaudio:
            return Backend::CoreAudio;
        case ma_backend_sndio:
            return Backend::SndIO;
        case ma_backend_audio4:
            return Backend::Audio4;
        case ma_backend_oss:
            return Backend::OSS;
        case ma_backend_pulseaudio:
            return Backend::PulseAudio;
        case ma_backend_alsa:
            return Backend::ALSA;
        case ma_backend_jack:
            return Backend::JACK;
        case ma_backend_aaudio:
            return Backend::AAudio;
        case ma_backend_opensl:
            return Backend::OpenSL;
        case ma_backend_webaudio:
            return Backend::WebAudio;
        case ma_backend_null:
            return Backend::Null;
        case ma_backend_custom:
        default:
            return Backend::Unknown;
    }
}

ma_backend getMaBackend(Backend backend)
{
    switch (backend)
    {
        case Backend::WASAPI:
            return ma_backend_wasapi;
        case Backend::DirectSound:
            return ma_backend_dsound;
        case Backend::WinMM:
            return ma_backend_winmm;
        case Backend::CoreAudio:
            return ma_backend_coreaudio;
        case Backend::SndIO:
            return ma_backend_sndio;
        case Backend::Audio4:
            return ma_backend_audio4;
        case Backend::OSS:
            return ma_backend_oss;
        case Backend::PulseAudio:
            return ma_backend_pulseaudio;
        case Backend::ALSA:
            return ma_backend_alsa;
        case Backend::JACK:
            return ma_backend_jack;
        case Backend::AAudio:
            return ma_backend_aaudio;
        case Backend::OpenSL:
            return ma_backend_opensl;
        case Backend::WebAudio:
            return ma_backend_webaudio;
        case Backend::Null:
        case Backend::Unknown:
        default:
            return ma_backend_null;
    }
}

Vector<Backend> probeAvailableBackends()
{
    auto backends = Vector<Backend> {};

    for (auto i = 0; i < MA_BACKEND_COUNT; ++i)
    {
        auto candidate = static_cast<ma_backend>(i);

        if (candidate == ma_backend_null || candidate == ma_backend_custom)
            continue;

        if (!ma_is_backend_enabled(candidate))
            continue;

        auto probe = ma_context {};

        if (ma_context_init(&candidate, 1, nullptr, &probe) != MA_SUCCESS)
            continue;

        ma_context_uninit(&probe);
        backends.add(getBackend(candidate));
    }

    return backends;
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
