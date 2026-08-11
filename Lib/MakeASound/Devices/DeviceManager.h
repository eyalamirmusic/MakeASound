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

    // Probed once and remembered, so a JACK server started after the manager was
    // constructed is not picked up; construct another one to look again.
    Vector<Backend> getAvailableBackends() const;

    // Never Unknown once construction succeeded — the default is one of them.
    Backend getBackend() const;

    // Device ids and channel counts are per-API: this stops the stream and drops
    // the config instead of carrying it across, so follow with getDefaultConfig()
    // and start(). A backend that won't come up enumerates nothing.
    Error setBackend(Backend backendToUse);

    // Only the sides that exist are filled in; a machine with neither gives a config
    // that start() reports as NO_DEVICES_FOUND rather than opening.
    StreamConfig getDefaultConfig() const;

    // A failure leaves no stream running and the manager usable. A config naming a
    // device that is merely busy comes back on its own once it frees up.
    Error setConfig(const StreamConfig& configToUse);
    Error start(const StreamConfig& configToUse, const Callback& cb);
    void stop() const;

    bool isRunning() const;
    Error getLastError() const;

    // Runs on an OS audio thread — on macOS from a Core Audio property listener,
    // and sometimes while recovery holds the device, so calling any DeviceManager
    // method from it can deadlock. Set it before start().
    void setNotificationCallback(const NotificationCallback& cb) const;

    // On by default: a device stopped by the OS (sample-rate change, unplug,
    // reclaim) is re-opened automatically. Turn it off to own that decision, e.g.
    // to show "device lost" rather than silently re-opening.
    void setAutoRecover(bool shouldRecover) const;

    long getStreamLatency() const;
    int getStreamSampleRate() const;

private:
    Error openStream();

    AudioCallbackInfo prevInfo;
    Callback callback;
    StreamConfig config;

    OwningPointer<MiniAudio::DeviceManager> pimpl;
};

} // namespace MakeASound
