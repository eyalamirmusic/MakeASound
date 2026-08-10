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

StreamConfig DeviceManager::getDefaultConfig() const
{
    auto defaultConfig = StreamConfig();

    auto input = getDefaultInputDevice();
    auto output = getDefaultOutputDevice();

    // A side the machine doesn't have stays unset rather than carrying a blank
    // DeviceInfo. Asking for a duplex stream on a desktop with no microphone fails the
    // whole open, taking the outputs down with the input that was never there.
    if (output.hasChannels(false))
        defaultConfig.output = StreamParameters(output, false);

    if (input.hasChannels(true))
        defaultConfig.input = StreamParameters(input, true);

    defaultConfig.sampleRate = pickCompatibleSampleRate(output, input);
    defaultConfig.maxBlockSize = 512;
    defaultConfig.options = StreamOptions {};

    return defaultConfig;
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
    // Straight onto the backend rather than kept here and forwarded at openStream, so
    // it survives every re-open the host makes and is in place before the first one.
    pimpl->notificationCallback = cb;
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

long DeviceManager::getStreamLatency() const
{
    return pimpl->getStreamLatency();
}

int DeviceManager::getStreamSampleRate() const
{
    return pimpl->getStreamSampleRate();
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
