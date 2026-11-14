#pragma once

#include "AAXCommon.h"
#include "AlgorithmContext.h"
#include "Parameters.h"
#include "../Core/Description.h"
#include "../Host/PluginWrapper.h"
#include "../../MIDI/MidiParser.h"
#include "../../Realtime/SPSCQueue.h"
#include "../../Realtime/SpinLock.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace MakeASound::AAX
{

// The data model every plugin ships as, over a PluginWrapper. The instance finds
// its plugin by the effect id the host created it for, and the process procedure
// reaches it through the private data ResetFieldData fills.
class Adapter
    : public AAX_CEffectParameters
    , public HostEditListener
{
public:
    // Over module() under moduleBundleId(), as the host creates one.
    Adapter();

    // Over another module and bundle id: the plugin is the one whose effect id
    // (aaxEffectIdFor) the controller names. The module must outlive the adapter.
    Adapter(const ModuleDescription& moduleToUse, std::string_view bundleIdToUse);

    ~Adapter() override;

    // kAAX_ProcPtrID_Create_EffectParameters
    static AAX_IEffectParameters* AAX_CALLBACK create();

    // The module this binary ships, described once.
    static const ModuleDescription& module();

    // Null until EffectInit found the plugin.
    PluginWrapper* wrapper() noexcept { return pluginWrapper.get(); }

    // The plugin index an AAX parameter id names, -1 unless the adapter
    // registered it (isRegistered). A binary search: allocation-free.
    int indexOf(AAX_CParamID id) const noexcept;

    // AAX_CEffectParameters
    AAX_Result Initialize(IACFUnknown* controller) override;
    AAX_Result Uninitialize() override;
    AAX_Result EffectInit() override;
    AAX_Result ResetFieldData(AAX_CFieldIndex index,
                              void* data,
                              uint32_t size) const override;

    AAX_Result GetMasterBypassParameter(AAX_IString* id) const override;

    // Marks the parameter dirty; GenerateCoefficients decides when it lands.
    AAX_Result UpdateParameterNormalizedValue(AAX_CParamID id,
                                              double value,
                                              AAX_EUpdateSource source) override;

    // The dirty values: automation playback (a non-zero automation timestamp)
    // is queued for the buffer the host delivers its state number in; anything
    // else lands in the wrapper now, unless a gesture holds the parameter.
    AAX_Result GenerateCoefficients() override;

    AAX_Result GetNumberOfChunks(int32_t* count) const override;
    AAX_Result GetChunkIDFromIndex(int32_t index, AAX_CTypeID* id) const override;
    AAX_Result GetChunkSize(AAX_CTypeID id, uint32_t* size) const override;
    AAX_Result GetChunk(AAX_CTypeID id, AAX_SPlugInChunk* chunk) const override;
    AAX_Result SetChunk(AAX_CTypeID id, const AAX_SPlugInChunk* chunk) override;
    AAX_Result CompareActiveChunk(const AAX_SPlugInChunk* chunk,
                                  AAX_CBoolean* isEqual) const override;

    // The base's parameter changes plus every stateChanged(), so Pro Tools marks
    // the session modified for state that is not a parameter.
    AAX_Result GetNumberOfChanges(int32_t* numChanges) const override;

    // Any thread (the SDK calls it off the main thread): host values stuck while
    // render is not running, the latency and the values the plugin moved.
    AAX_Result TimerWakeup() override;

    // After the base's: AAX_eNotificationEvent_TrackNameChanged becomes the
    // plugin's track name, the one thing about the track Pro Tools tells, and
    // Entering/ExitingOfflineMode every later block's isOffline.
    AAX_Result NotificationReceived(AAX_CTypeID type,
                                    const void* data,
                                    uint32_t size) override;

    // One instance's block, from the process procedure. Audio thread.
    void render(AlgorithmContext& context) noexcept;

    static constexpr int queueCapacity = 4096;

    // Automated values that found the queue full or their packet refused, and
    // so landed early.
    int queueOverflows() const noexcept { return overflows.load(); }

    // How long host values may stay pending with nothing consumed before
    // TimerWakeup lands them; a second by default.
    void setStallThreshold(std::chrono::steady_clock::duration threshold) noexcept
    {
        stallThreshold = threshold;
    }

    // HostEditListener, message thread
    void beginParameterEdit(int index) noexcept override;
    void performParameterEdit(int index, float normalized) noexcept override;
    void endParameterEdit(int index) noexcept override;
    void latencyChanged() noexcept override;
    void parameterValuesChanged() noexcept override;
    void stateChanged() noexcept override;

private:
    // One automated value, under the state number its packet carries.
    struct QueuedValue
    {
        int64_t state = 0;
        int32_t index = 0;
        float value = 0.f;
    };

    // Per plugin index: what the host last saw the plugin hold, whether a host
    // value is dirty, the newest state it was queued under and the state below
    // which its queue is stale.
    struct HostValue
    {
        std::atomic<float> baseline {0.f};
        std::atomic<bool> dirty {false};
        std::atomic<int64_t> lastQueued {0};
        std::atomic<int64_t> supersededBelow {0};

        // Under dataModelMutex: the dirty value and the newest queued one.
        float pending = 0.f;
        float queued = 0.f;
    };

    AAX_Result createWrapper();
    void registerParameters();

    // Set once EffectInit built everything; what a call off the main thread
    // checks before touching the wrapper.
    bool isReady() const noexcept { return ready.load(std::memory_order_acquire); }

    HostValue& hostValue(int index) const noexcept
    {
        return hostValues[static_cast<size_t>(index)];
    }

    void renderSlice(AlgorithmContext& context,
                     const Playhead& playhead,
                     int start,
                     int numSamples) noexcept;
    void notifyHostIfLatencyChanged() noexcept;

    // Audio thread: every queued value up to the delivered state, at offset 0.
    void takeHostValues(const AlgorithmContext& context, int numSamples) noexcept;

    // Under dataModelMutex. Queues the dirty values under one state number, or
    // lands each one now: its dirty value if any, else its newest queued one.
    void queueDirty(AAX_IController& controller) noexcept;
    void writeNow(const Vector<int>& indices) noexcept;
    void clearDirty() noexcept;

    // The parameters' queued values give way to a write or a load.
    void supersede(const Vector<int>& indices) noexcept;

    // Lands the dirty values now, as a GenerateCoefficients with no timestamp.
    void flushDirty() noexcept;

    // Lands every pending host value now and supersedes its queue: before a
    // load, which then wins, and when the watchdog finds them stuck.
    void settlePending() noexcept;

    // A host value the plugin does not hold yet: dirty, or queued and unread.
    bool isHostPending(int index) const noexcept;

    // Lands every pending host value once nothing has been consumed for the
    // stall threshold.
    void settleIfStalled() noexcept;

    // The AAX parameters take what the plugin holds; only the ones it moved
    // since the last look unless `all`. Never one with a host value pending.
    void mirrorParametersToHost(bool all);

    const ModuleDescription& described;
    std::string bundleId;

    OwningPointer<PluginWrapper> pluginWrapper;
    std::atomic<bool> ready {false};

    // Alive with the adapter, for work posted to the message thread.
    std::shared_ptr<bool> lifetime = std::make_shared<bool>(true);

    int inputChannels = 0;
    int outputChannels = 0;
    double sampleRate = 44100.0;
    bool hasMidiIn = false;
    bool hasMidiOut = false;

    // Registered plugin indices in declaration order, the same sorted by their
    // AAX id, each one's AAX id as registered (registeredId) by plugin index,
    // empty for the rest, and each one's HostValue.
    Vector<int> registered;
    Vector<int> byParamId;
    Vector<std::string> paramIds;
    std::unique_ptr<HostValue[]> hostValues;

    // Update, GenerateCoefficients, the settle and a load's drop take it; the
    // lists are reserved for every parameter.
    std::mutex dataModelMutex;
    Vector<int> dirtyList;
    Vector<int> writeList;
    int64_t nextState = 1;

    // Pushed by GenerateCoefficients, popped by render, which skips its drain
    // rather than wait while a supersede holds consumeLock.
    SPSCQueue<QueuedValue, queueCapacity> hostQueue;
    EA::Locks::PrimitiveSpinLock consumeLock;
    std::atomic<int64_t> consumedThrough {0};
    std::atomic<int> overflows {0};

    // TimerWakeup's watch on consumedThrough.
    std::chrono::steady_clock::duration stallThreshold = std::chrono::seconds {1};
    bool watching = false;
    int64_t watchedConsumed = 0;
    std::chrono::steady_clock::time_point idleSince;

    Vector<const float*> inputTable;
    Vector<float*> outputTable;

    MIDI::Buffer incoming;
    std::array<std::uint8_t, MIDI::SysEx::maxBytes> sysExBuffer {};
    MidiParser parser;
    int64_t renderedSamples = 0;

    // GetChunkSize saves, GetChunk copies: the SDK pairs the two calls.
    mutable std::mutex chunkMutex;
    mutable std::string savedChunk;

    // What the host was last told.
    std::atomic<int> lastReportedLatency {-1};

    // How many times the plugin marked its non-parameter state changed.
    std::atomic<int32_t> stateChanges {0};

    // Between Pro Tools' Entering and ExitingOfflineMode (a bounce).
    std::atomic<bool> offline {false};
};

} // namespace MakeASound::AAX
