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

    // Open the stream and start it, reporting rather than throwing on failure. When
    // the config names a device that can't be opened right now, the recovery worker
    // keeps trying in the background, so a device that is merely busy comes back on
    // its own. A config that names nothing is not retried: nothing would change.
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

    // Records an outcome in lastError and hands it back, so the failing line stays a
    // single `return setError(...)`.
    Error setError(Error error);

    // The *Locked variants assume deviceMutex is already held. The public entry points
    // take it; so does the recovery worker, which is the reason it exists at all — the
    // device is now opened and closed from two threads.
    Error startLocked();
    void stopLocked();
    Error openStreamLocked();

    // Recovery worker: waits for a stopped notification, then re-opens the stream until
    // it comes back. Runs on its own thread because re-opening from the notification
    // callback deadlocks — on macOS that callback arrives inside a Core Audio property
    // listener, and tearing the device down from there waits on the lock the listener
    // itself holds.
    void ensureRecoveryThread();
    void requestRecovery();
    void runRecovery();
    bool tryReopen();
    void notifyHost(DeviceNotification notification);

    // Whether the stream is meant to be running but has gone quiet. Not every way a
    // device dies reaches us as a notification — a driver can simply stop calling back
    // while the OS still believes the unit is running — so the callback itself is the
    // only thing that can be trusted to say audio is alive.
    bool isStarved() const;

    // Re-point the config at devices in the freshly enumerated cache. Cache ids are
    // enumeration order, so they shift whenever a device appears or disappears; the
    // name is what survives.
    void repointConfigToCache();

    ma_context context {};
    bool contextInitialised = false;

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

    // The device is opened at its full native channel count so CoreAudio hands
    // us every physical channel untouched; the callback then copies only the
    // selected slice [first, first + count) to/from the user. These are the
    // native strides of the interleaved buffers miniaudio passes the callback.
    int captureChannels = 0;
    int playbackChannels = 0;

    int inputFirstChannel = 0;
    int inputChannelCount = 0;
    int outputFirstChannel = 0;
    int outputChannelCount = 0;

    ma_uint64 framesElapsed = 0;

    // Tearing a device down makes the OS report it as stopped, which is indistinguishable
    // at the callback from the OS stopping it on its own. Raised for the length of our own
    // teardown so those self-inflicted notifications are dropped and `Stopped` keeps
    // meaning "the device went away".
    std::atomic<bool> stopping {false};

    // A notification landed and no audio callback has run since. Consumed by the next
    // callback to raise AudioCallbackInfo::dirty, so a host that registered no
    // notification callback still learns the stream changed under it.
    std::atomic<bool> notificationPending {false};

    // Whether a stream is open and started. Distinct from shouldRun, which is what the
    // host asked for: a machine with no device has shouldRun set and this clear. Atomic
    // because a host polls it to render "no audio device" while recovery is re-opening
    // under deviceMutex, and waiting on that lock to answer would stall the UI.
    std::atomic<bool> streamRunning {false};

    // Why the last open or start didn't work, kept for a host that wants to say so.
    std::atomic<Error> lastError {Error::NoError};

    // Guards the device itself and the cache the re-open reads, both of which the
    // recovery worker touches concurrently with the host.
    std::mutex deviceMutex;

    // Whether a stream is meant to be running. Recovery re-opens only while this is
    // true, so a stop() racing a dying device wins and stays stopped. Atomic because the
    // watchdog reads it without taking deviceMutex, which the re-open holds.
    std::atomic<bool> shouldRun {false};

    // Steady-clock milliseconds at the most recent data callback, and at the most recent
    // open. The watchdog compares against them to tell a live stream from a dead one.
    std::atomic<std::int64_t> lastCallbackMs {0};

    std::thread recoveryThread;
    std::mutex recoveryMutex;
    std::condition_variable recoveryCv;
    bool recoveryRequested = false;
    bool recoveryQuit = false;
};

} // namespace MakeASound::MiniAudio
