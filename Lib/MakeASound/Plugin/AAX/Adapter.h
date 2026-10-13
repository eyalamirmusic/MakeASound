#pragma once

#include "AAXCommon.h"
#include "AlgorithmContext.h"
#include "Parameters.h"
#include "../Core/Description.h"
#include "../Host/PluginWrapper.h"
#include "../../MIDI/MidiParser.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

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
    Adapter() = default;
    ~Adapter() override;

    // kAAX_ProcPtrID_Create_EffectParameters
    static AAX_IEffectParameters* AAX_CALLBACK create();

    // The module this binary ships, described once.
    static const ModuleDescription& module();

    // Null until EffectInit found the plugin.
    PluginWrapper* wrapper() noexcept { return pluginWrapper.get(); }

    // The plugin index an AAX parameter id names, -1 unless it is host-exposed.
    int indexOf(AAX_CParamID id) const noexcept;

    // AAX_CEffectParameters
    AAX_Result Initialize(IACFUnknown* controller) override;
    AAX_Result Uninitialize() override;
    AAX_Result EffectInit() override;
    AAX_Result ResetFieldData(AAX_CFieldIndex index,
                              void* data,
                              uint32_t size) const override;

    AAX_Result GetMasterBypassParameter(AAX_IString* id) const override;

    // A host write lands in the wrapper unless a gesture holds the parameter.
    AAX_Result UpdateParameterNormalizedValue(AAX_CParamID id,
                                              double value,
                                              AAX_EUpdateSource source) override;

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

    // Message thread: the latency and the values the plugin moved on its own.
    AAX_Result TimerWakeup() override;

    // One instance's block, from the process procedure. Audio thread.
    void render(AlgorithmContext& context) noexcept;

    // HostEditListener, message thread
    void beginParameterEdit(int index) noexcept override;
    void performParameterEdit(int index, float normalized) noexcept override;
    void endParameterEdit(int index) noexcept override;
    void latencyChanged() noexcept override;
    void parameterValuesChanged() noexcept override;
    void stateChanged() noexcept override;

private:
    AAX_Result createWrapper();
    void registerParameters();
    void renderSlice(AlgorithmContext& context,
                     const Playhead& playhead,
                     int start,
                     int numSamples) noexcept;
    void notifyHostIfLatencyChanged() noexcept;

    // The AAX parameters take what the plugin holds; only the ones it moved
    // since the last look unless `all`.
    void mirrorParametersToHost(bool all);

    OwningPointer<PluginWrapper> pluginWrapper;

    int inputChannels = 0;
    int outputChannels = 0;
    double sampleRate = 44100.0;
    bool hasMidiIn = false;
    bool hasMidiOut = false;

    // Host-exposed plugin indices in declaration order, each one's AAX id by
    // plugin index, and what each held when last mirrored to the host.
    Vector<int> exposed;
    Vector<ParamId> paramIds;
    std::unique_ptr<std::atomic<float>[]> lastPluginValue;

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
};

} // namespace MakeASound::AAX
