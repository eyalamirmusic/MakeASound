#pragma once

#include "Analyser.h"
#include "AudioSession.h"

#include <MakeASound/MakeASound.h>

#include <atomic>
#include <optional>
#include <thread>

namespace AudioProbe
{
namespace MS = MakeASound;

struct DeviceEvent
{
    MS::DeviceNotification type {};

    // How it got here. The realtime callback fires on whatever OS thread raised the
    // notification; drainNotifications() hands the same one over on the thread that
    // asked, which is what a UI needs and what it used to have to arrange itself.
    bool viaQueue = false;

    // The thread it was delivered on. A UI that trusted an audio thread would be
    // touching its widgets from one.
    bool onMainThread = false;
};

struct StreamStats
{
    int sampleRate = 0;
    int blockSize = 0;
    int lastNumSamples = 0;
    int inputs = 0;
    int outputs = 0;
    int latency = 0;
    long long callbacks = 0;
    int dirtyBlocks = 0;
    int underflows = 0;
    int overflows = 0;
    bool firstBlockDirty = false;

    // The probe's own deliberate missed deadline has happened, so whether a dropout
    // is visible at all is now answerable.
    bool stallDone = false;
};

// The audio side of the probe: a tone generator wired through MakeASound, with
// everything a probe wants to ask about recorded on the way past.
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    MS::DeviceManager& getManager() { return *manager; }
    Analyser& getAnalyser() { return analyser; }

    // The session at each of the three moments that answer "who owns it": before
    // the manager existed, after its constructor ran, and once a playback-only
    // stream is up.
    const MS::SessionState& getSessionBeforeConstruction() const { return before; }
    const MS::SessionState& getSessionAfterConstruction() const { return after; }
    const MS::SessionState& getSessionAfterStart() const { return afterStart; }

    MS::Error start();

    void setOutputDevice(int deviceId);

    // Which pair of the device's outputs carries the tone. Everything else on the
    // device stays silent.
    void setOutputChannels(int firstChannel, int count);
    void setSampleRate(int rate);
    void setBlockSize(int size);
    void setInputEnabled(bool enabled);
    void setAutoRecover(bool enabled);

    bool isInputEnabled() const { return config.input.has_value(); }
    bool isAutoRecovering() const { return autoRecover; }

    int getRequestedBlockSize() const { return config.maxBlockSize; }
    int getRequestedSampleRate() const { return config.sampleRate; }
    const MS::StreamConfig& getConfig() const { return config; }
    MS::Error getLastError() const { return lastError; }

    StreamStats getStats() const;

    // Both delivery paths, drained on whatever thread calls this: what the realtime
    // callback pushed, and what MakeASound queued for a caller to come and get.
    MS::Vector<DeviceEvent> drainEvents();

    std::atomic<float> toneHz {220.f};
    std::atomic<float> levelGain {0.03f};
    std::atomic<bool> monitorInput {false};

private:
    void audioCallback(MS::AudioCallbackInfo& info);
    void holdPastTheDeadline(const MS::AudioCallbackInfo& info);
    MS::Error reopen();

    MS::SessionState before;
    MS::SessionState after;
    MS::SessionState afterStart;

    std::optional<MS::DeviceManager> manager;
    MS::StreamConfig config;
    MS::Error lastError = MS::Error::NoError;
    bool autoRecover = true;

    Analyser analyser;

    std::thread::id mainThread = std::this_thread::get_id();
    MS::SPSCQueue<DeviceEvent, 64> events;

    std::atomic<int> statSampleRate {0};
    std::atomic<int> statBlockSize {0};
    std::atomic<int> statNumSamples {0};
    std::atomic<int> statInputs {0};
    std::atomic<int> statOutputs {0};
    std::atomic<int> statLatency {0};
    std::atomic<long long> statCallbacks {0};
    std::atomic<int> statDirty {0};
    std::atomic<int> statUnderflows {0};
    std::atomic<int> statOverflows {0};
    std::atomic<bool> statFirstBlockDirty {false};
    std::atomic<bool> stalled {false};

    float phase = 0.f;
    unsigned noiseState = 0x9e3779b9u;
};

} // namespace AudioProbe
