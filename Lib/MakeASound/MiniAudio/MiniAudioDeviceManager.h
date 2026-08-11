#pragma once

#include "MiniAudio-Backend.h"

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <mutex>
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

    long getStreamLatency() const;
    int getStreamSampleRate() const;

    void onCallback(void* output, const void* input, ma_uint32 frameCount);
    void onNotification(ma_device_notification_type type);

    Callback callback;
    NotificationCallback notificationCallback;
    StreamConfig config;

    std::atomic<bool> autoRecover {true};

private:
    DeviceInfo buildDeviceInfo(const ma_device_info& enumInfo,
                               ma_device_type type,
                               int assignedId);
    Error refreshDeviceCache();
    const ma_device_id* findDeviceId(int makeASoundId) const;

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

    // Not every way a device dies reaches us as a notification — a driver can stop
    // calling back while the OS still believes the unit is running.
    bool isStarved() const;

    // Cache ids are enumeration order, so they shift whenever a device appears or
    // disappears; the name is what survives.
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

    // Steady-clock ms at the last data callback — the watchdog's proof of life.
    std::atomic<std::int64_t> lastCallbackMs {0};

    std::thread recoveryThread;
    std::mutex recoveryMutex;
    std::condition_variable recoveryCv;
    bool recoveryRequested = false;
    bool recoveryQuit = false;
};

} // namespace MakeASound::MiniAudio
