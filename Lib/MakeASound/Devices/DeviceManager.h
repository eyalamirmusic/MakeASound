#pragma once

#include "../Common/Common.h"
#include "DeviceInfo.h"

namespace MakeASound
{
namespace MiniAudio
{
struct DeviceManager;
}

class DeviceManager
{
public:
    DeviceManager();
    ~DeviceManager();

    Vector<DeviceInfo> getDevices() const;
    DeviceInfo getDefaultInputDevice() const;
    DeviceInfo getDefaultOutputDevice() const;
    StreamConfig getDefaultConfig() const;

    void setConfig(const StreamConfig& configToUse);
    void start(const StreamConfig& configToUse, const Callback& cb);
    void stop() const;

    // Called when the device changes state on its own — see DeviceNotification. Purely
    // informational: recovery from a stop happens with or without it. Set it before
    // start(); it is not synchronised against a live stream.
    //
    // Runs on an OS audio thread — on macOS from inside a Core Audio property listener,
    // and sometimes while recovery holds the device. Calling any DeviceManager method
    // from it can deadlock. Post the work to your own thread and act on it there.
    void setNotificationCallback(const NotificationCallback& cb) const;

    // Whether a device stopped by the OS is re-opened automatically. On by default: a
    // host that does nothing keeps receiving audio across a sample-rate change made by
    // another app, an unplug, or the OS reclaiming the device. Turn it off to own that
    // decision yourself — e.g. to show "device lost" rather than silently re-opening.
    void setAutoRecover(bool shouldRecover) const;

    long getStreamLatency() const;
    int getStreamSampleRate() const;

private:
    int openStream();

    AudioCallbackInfo prevInfo;
    Callback callback;
    StreamConfig config;

    OwningPointer<MiniAudio::DeviceManager> pimpl;
};

} // namespace MakeASound
