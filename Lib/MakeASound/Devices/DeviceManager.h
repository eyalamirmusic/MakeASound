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

    // The system audio APIs this machine can actually be driven through — see Backend.
    // Probed once and remembered, so a JACK server started after the manager was
    // constructed is not picked up; construct another one to look again.
    Vector<Backend> getAvailableBackends() const;

    // Which API the manager is enumerating and opening through right now. Never
    // Unknown once construction succeeded: the machine's default is still one of them.
    Backend getBackend() const;

    // Switch to another API. Everything below this façade is per-API — device ids,
    // channel counts, even which devices exist — so the switch stops any running
    // stream and forgets the current config rather than trying to carry it across.
    // Follow it with getDefaultConfig() and start() to get audio back.
    //
    // An API that won't come up leaves the manager with no context: it enumerates
    // nothing and refuses to open, which getLastError() and the returned Error say.
    // Switching back to one that works recovers it.
    Error setBackend(Backend backendToUse);

    // The stream this machine would open on its own. Only the sides that exist are
    // filled in: a desktop with no microphone gives an output-only config, and a
    // machine with no audio hardware at all gives one with neither side set, which
    // start() reports as NO_DEVICES_FOUND rather than opening.
    StreamConfig getDefaultConfig() const;

    // Both report why they didn't work instead of throwing — see Error. A failure
    // leaves no stream running and the manager usable; the host decides whether to
    // show it, pick another device, or wait. When the config names a device that is
    // merely busy, the stream also comes back on its own once it frees up.
    Error setConfig(const StreamConfig& configToUse);
    Error start(const StreamConfig& configToUse, const Callback& cb);
    void stop() const;

    // Whether audio is flowing right now. The one thing worth asking after a start
    // that returned an error, and after a device has come or gone.
    bool isRunning() const;

    // Why the last start/setConfig didn't work. NoError once one has succeeded.
    Error getLastError() const;

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
    Error openStream();

    AudioCallbackInfo prevInfo;
    Callback callback;
    StreamConfig config;

    OwningPointer<MiniAudio::DeviceManager> pimpl;
};

} // namespace MakeASound
