#pragma once

#include "MiniAudio-Backend.h"

#include <atomic>
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

    void start();
    void stop();
    int openStream(const StreamConfig& configToUse);

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
    void refreshDeviceCache();
    const ma_device_id* findDeviceId(int makeASoundId) const;

    // The *Locked variants assume deviceMutex is already held. The public entry points
    // take it; so does the recovery worker, which is the reason it exists at all — the
    // device is now opened and closed from two threads.
    void startLocked();
    void stopLocked();
    int openStreamLocked();

    // Recovery worker: waits for a stopped notification, then re-opens the stream until
    // it comes back. Runs on its own thread because re-opening from the notification
    // callback deadlocks — on macOS that callback arrives inside a Core Audio property
    // listener, and tearing the device down from there waits on the lock the listener
    // itself holds.
    void ensureRecoveryThread();
    void requestRecovery();
    void runRecovery();
    bool tryReopen();

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

    // Guards the device itself and the cache the re-open reads, both of which the
    // recovery worker touches concurrently with the host.
    std::mutex deviceMutex;

    // Whether a stream is meant to be running. Recovery re-opens only while this is
    // true, so a stop() racing a dying device wins and stays stopped.
    bool shouldRun = false;

    std::thread recoveryThread;
    std::mutex recoveryMutex;
    std::condition_variable recoveryCv;
    bool recoveryRequested = false;
    bool recoveryQuit = false;
};

} // namespace MakeASound::MiniAudio
