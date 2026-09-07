#pragma once

#include "MiniAudio-Backend.h"

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace MakeASound::MiniAudio
{

void audioCallback(ma_device* device,
                   void* output,
                   const void* input,
                   ma_uint32 frameCount);

void deviceNotificationCallback(const ma_device_notification* notification);

struct DeviceManager
{
    DeviceManager();
    ~DeviceManager();

    Vector<DeviceInfo> getDevices();
    DeviceInfo getDefaultInputDevice();
    DeviceInfo getDefaultOutputDevice();

    // Probed once and remembered: each candidate costs a real connection attempt (a
    // PulseAudio socket, a JACK handshake), and the set is far more stable than the
    // device list.
    const Vector<Backend>& getAvailableBackends();
    Backend getBackend() const;

    // Whatever was running stops: the device and the device cache belong to the old
    // context.
    Error setBackend(Backend backendToUse);

    // On failure the recovery worker keeps retrying in the background, unless the
    // config names no device at all — retrying that would change nothing.
    Error start(const StreamConfig& configToUse);
    void stop();

    bool isRunning() const;
    Error getLastError() const;

    int getStreamLatency() const;
    int getStreamSampleRate() const;
    int getStreamBlockSize() const;

    void onCallback(void* output, const void* input, ma_uint32 frameCount);
    void onNotification(ma_device_notification_type type);

    // Everything queued since the last call, in order, for whatever thread asks.
    Vector<DeviceNotification> takeNotifications();

    Callback callback;
    NotificationCallback notificationCallback;
    StreamConfig config;

    // Applied by every open, recovery's included, so an interruption that hands the
    // session back deactivated cannot leave it configured for someone else.
    SessionConfig sessionConfig;

    std::atomic<bool> autoRecover {true};

private:
    DeviceInfo buildDeviceInfo(const ma_device_info& enumInfo,
                               ma_device_type type,
                               int assignedId);
    Error refreshDeviceCache();
    void resolveDefaults();
    const ma_device_id* findDeviceId(int makeASoundId) const;

    // Ids have to outlive an enumeration: a host caches a DeviceInfo and opens it
    // later, and a hotplug renumbers every device that came after the one that
    // moved. The name is what a device keeps, so an id is a slot in this registry,
    // handed back to the same name every time it is enumerated. Per-API, like the
    // ids themselves — setBackend clears it.
    int idForDevice(const std::string& name, const Vector<int>& usedIds);
    Vector<std::string> idRegistry;

    // Backend::Unknown brings the context up on miniaudio's default order;
    // currentBackend records which API answered.
    Error initContext(Backend backendToUse);

    Error setError(Error error);

    // The *Locked variants assume deviceMutex is already held.
    Error startLocked();
    void stopLocked();
    Error openStreamLocked();

    // Own thread: re-opening from the notification callback deadlocks — on macOS it
    // arrives inside a Core Audio property listener, and tearing the device down
    // there waits on the lock the listener itself holds.
    void ensureRecoveryThread();
    void requestRecovery();
    void runRecovery();
    bool tryReopen();
    void notifyHost(DeviceNotification notification);

    // miniaudio's data callback carries no status of its own, and the backends that
    // know about xruns handle them internally, so the clock is what is left.
    AudioCallbackStatus getCallbackStatus(std::int64_t previousUs,
                                          std::int64_t nowUs,
                                          int frames) const;

    // Not every way a device dies reaches us as a notification — a driver can stop
    // calling back while the OS still believes the unit is running.
    bool isStarved() const;

    // A device that comes back is the same id but not the same backend handle, and
    // may come back with other channel counts or rates; the config is re-pointed at
    // the fresh cache entry so the re-open negotiates against what is there now.
    void repointConfigToCache();

    ma_context context {};
    bool contextInitialised = false;

    Backend currentBackend = Backend::Unknown;
    Vector<Backend> availableBackends;
    bool backendsProbed = false;

    ma_device device {};
    bool deviceInitialised = false;

    struct CachedDevice
    {
        int id {};
        ma_device_id playbackId {};
        ma_device_id captureId {};
        bool hasPlayback = false;
        bool hasCapture = false;
        DeviceInfo info {};
    };

    Vector<CachedDevice> deviceCache;

    Vector<float> inputScratch;
    Vector<float> outputScratch;

    // Native strides of the interleaved buffers miniaudio passes the callback: the
    // device is opened at full native width and only a slice reaches the user.
    int captureChannels = 0;
    int playbackChannels = 0;

    int inputFirstChannel = 0;
    int inputChannelCount = 0;
    int outputFirstChannel = 0;
    int outputChannelCount = 0;

    ma_uint64 framesElapsed = 0;

    // Read once per open: the property read is a HAL round-trip and the audio
    // callback asks for the latency on every block.
    int routeLatencyFrames = 0;

    // Our own teardown makes the OS report a stop, indistinguishable at the callback
    // from the device going away. Raised across teardown so those are dropped.
    std::atomic<bool> stopping {false};

    // Consumed by the next audio callback to raise AudioCallbackInfo::dirty, so a
    // host with no notification callback still learns the stream changed under it.
    std::atomic<bool> notificationPending {false};

    // A stream is open and started, as opposed to shouldRun, which is what the host
    // asked for: a machine with no device has shouldRun set and this clear.
    std::atomic<bool> streamRunning {false};

    std::atomic<Error> lastError {Error::NoError};

    // Guards the device and the device cache, both touched by the recovery worker.
    std::mutex deviceMutex;

    // What the host asked for. Recovery re-opens only while true, so a stop() racing
    // a dying device wins and stays stopped.
    std::atomic<bool> shouldRun {false};

    // Steady-clock microseconds at the last data callback: the watchdog's proof of
    // life, and the gap a dropout shows up in.
    std::atomic<std::int64_t> lastCallbackUs {0};

    // Delivered to the notification callback on whatever OS thread raised them, and
    // queued here for a host thread that would rather not be that thread.
    std::mutex notificationMutex;
    Vector<DeviceNotification> pendingNotifications;

    std::thread recoveryThread;
    std::mutex recoveryMutex;
    std::condition_variable recoveryCv;
    bool recoveryRequested = false;
    bool recoveryQuit = false;
};

} // namespace MakeASound::MiniAudio
