#include "DeviceManager.h"
#include "../MiniAudio/MiniAudioDeviceManager.h"

namespace MakeASound
{

DeviceManager::DeviceManager()
    : pimpl(EA::makeOwned<MiniAudio::DeviceManager>())
{
}

DeviceManager::~DeviceManager()
{
    stop();
}

Vector<DeviceInfo> DeviceManager::getDevices() const
{
    return pimpl->getDevices();
}

DeviceInfo DeviceManager::getDefaultInputDevice() const
{
    return pimpl->getDefaultInputDevice();
}

DeviceInfo DeviceManager::getDefaultOutputDevice() const
{
    return pimpl->getDefaultOutputDevice();
}

Vector<Backend> DeviceManager::getAvailableBackends() const
{
    return pimpl->getAvailableBackends();
}

Backend DeviceManager::getBackend() const
{
    return pimpl->getBackend();
}

Error DeviceManager::setBackend(Backend backendToUse)
{
    auto error = pimpl->setBackend(backendToUse);

    // The backend dropped its config; drop ours with it, and the remembered callback
    // shape, so the next stream's first callback reads as dirty rather than being
    // compared against one from a different API.
    config = {};
    prevInfo = {};

    return error;
}

StreamConfig DeviceManager::makeDefaultConfig(bool wantsOutput, bool wantsInput) const
{
    auto defaultConfig = StreamConfig();

    auto input = wantsInput ? getDefaultInputDevice() : DeviceInfo {};
    auto output = wantsOutput ? getDefaultOutputDevice() : DeviceInfo {};

    // A side the machine doesn't have stays unset: asking for a duplex stream on a
    // desktop with no microphone fails the whole open, taking the outputs down with
    // the input that was never there.
    if (output.hasChannels(false))
        defaultConfig.output = StreamParameters(output, false);

    if (input.hasChannels(true))
        defaultConfig.input = StreamParameters(input, true);

    defaultConfig.sampleRate = pickCompatibleSampleRate(output, input);
    defaultConfig.maxBlockSize = 512;
    defaultConfig.options = StreamOptions {};

    return defaultConfig;
}

StreamConfig DeviceManager::getDefaultOutputConfig() const
{
    return makeDefaultConfig(true, false);
}

StreamConfig DeviceManager::getDefaultInputConfig() const
{
    return makeDefaultConfig(false, true);
}

StreamConfig DeviceManager::getDefaultDuplexConfig() const
{
    return makeDefaultConfig(true, true);
}

StreamConfig DeviceManager::getDefaultConfig() const
{
    return getDefaultDuplexConfig();
}

void DeviceManager::setSessionConfig(const SessionConfig& sessionConfigToUse) const
{
    // Straight onto the backend, like the notification callback: every open reads it
    // there, including the ones recovery drives from its own thread.
    pimpl->sessionConfig = sessionConfigToUse;
}

SessionConfig DeviceManager::getSessionConfig() const
{
    return pimpl->sessionConfig;
}

Error DeviceManager::setConfig(const StreamConfig& configToUse)
{
    stop();
    config = configToUse;
    return openStream();
}

Error DeviceManager::start(const StreamConfig& configToUse, const Callback& cb)
{
    callback = cb;
    return setConfig(configToUse);
}

void DeviceManager::stop() const
{
    pimpl->stop();
}

void DeviceManager::setNotificationCallback(const NotificationCallback& cb) const
{
    // Straight onto the backend rather than forwarded at openStream, so it survives
    // every re-open and is in place before the first one.
    pimpl->notificationCallback = cb;
}

Vector<DeviceNotification> DeviceManager::drainNotifications() const
{
    return pimpl->takeNotifications();
}

void DeviceManager::setAutoRecover(bool shouldRecover) const
{
    pimpl->autoRecover = shouldRecover;
}

bool DeviceManager::isRunning() const
{
    return pimpl->isRunning();
}

Error DeviceManager::getLastError() const
{
    return pimpl->getLastError();
}

int DeviceManager::getStreamLatency() const
{
    return pimpl->getStreamLatency();
}

int DeviceManager::getStreamSampleRate() const
{
    return pimpl->getStreamSampleRate();
}

int DeviceManager::getStreamBlockSize() const
{
    return pimpl->getStreamBlockSize();
}

Error DeviceManager::openStream()
{
    if (!callback)
        return Error::INVALID_USE;

    auto actualCallback = [this](AudioCallbackInfo& info)
    {
        if (prevInfo != info)
        {
            prevInfo = info;
            info.dirty = true;
        }

        callback(info);
    };

    pimpl->callback = actualCallback;

    return pimpl->start(config);
}

} // namespace MakeASound
