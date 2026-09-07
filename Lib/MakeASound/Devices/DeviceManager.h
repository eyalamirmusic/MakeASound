#pragma once

#include "../Common/Common.h"
#include "DeviceInfo.h"
#include "AudioSession.h"

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

    // One per direction rather than one that guesses: claiming a capture side an app
    // never asked for costs it the microphone permission on every platform that has
    // one. A side the machine doesn't have is left unset, and a config with neither
    // is reported by start() as NO_DEVICES_FOUND rather than opened.
    StreamConfig getDefaultOutputConfig() const;
    StreamConfig getDefaultInputConfig() const;
    StreamConfig getDefaultDuplexConfig() const;

    // What the platform's audio session should be while a stream is open. Applied by
    // every open, so setting it after start() takes effect on the next one. Has no
    // effect where hasAudioSession() is false. See AudioSession.h.
    void setSessionConfig(const SessionConfig& sessionConfigToUse) const;
    SessionConfig getSessionConfig() const;

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

    // What the device actually runs, which is not always what was asked for. All 0
    // while no stream is open.
    int getStreamLatency() const;
    int getStreamSampleRate() const;
    int getStreamBlockSize() const;

private:
    StreamConfig makeDefaultConfig(bool wantsOutput, bool wantsInput) const;
    Error openStream();

    AudioCallbackInfo prevInfo;
    Callback callback;
    StreamConfig config;

    OwningPointer<MiniAudio::DeviceManager> pimpl;
};

} // namespace MakeASound
