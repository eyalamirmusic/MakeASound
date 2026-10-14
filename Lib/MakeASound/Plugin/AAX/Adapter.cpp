#include "Adapter.h"
#include "Conversion.h"
#include "Describe.h"
#include "State.h"
#include "../Core/Identity.h"
#include "../Realtime/MessageThread.h"

#include <AAX_CString.h>
#include <AAX_Exception.h>

#include <algorithm>
#include <chrono>

namespace MakeASound::AAX
{

namespace
{
// The SDK's ceiling for a Native process call; a larger block is rendered in
// slices of it rather than trusted.
constexpr auto maxBlockSize = 1 << AAX_eAudioBufferLengthNative_Max;

template <typename Fn>
AAX_Result guarded(Fn&& fn) noexcept
{
    try
    {
        return fn();
    }
    catch (const ::AAX::Exception::ResultError& error)
    {
        return error.Result();
    }
    catch (...)
    {
        return AAX_ERROR_UNKNOWN_EXCEPTION;
    }
}

MidiTimePoint timeAt(int64_t samples, double sampleRate) noexcept
{
    auto seconds = std::chrono::duration<double>(static_cast<double>(samples)
                                                 / std::max(1.0, sampleRate));
    return MidiTimePoint {
        std::chrono::duration_cast<MidiTimePoint::duration>(seconds)};
}
} // namespace

Adapter::Adapter()
    : Adapter(module(), moduleBundleId())
{
}

Adapter::Adapter(const ModuleDescription& moduleToUse,
                 std::string_view bundleIdToUse)
    : described(moduleToUse)
    , bundleId(bundleIdToUse)
{
}

Adapter::~Adapter()
{
    if (pluginWrapper)
        pluginWrapper->plugin().setHostEditListener(nullptr);
}

AAX_IEffectParameters* AAX_CALLBACK Adapter::create()
{
    return new Adapter;
}

const ModuleDescription& Adapter::module()
{
    static auto described = describeModule();
    return described;
}

AAX_Result Adapter::Initialize(IACFUnknown* controller)
{
    adoptHostMessageThread();
    return AAX_CEffectParameters::Initialize(controller);
}

AAX_Result Adapter::Uninitialize()
{
    if (pluginWrapper)
        pluginWrapper->plugin().setHostEditListener(nullptr);

    return AAX_CEffectParameters::Uninitialize();
}

AAX_Result Adapter::EffectInit()
{
    return guarded([this] { return createWrapper(); });
}

AAX_Result Adapter::createWrapper()
{
    auto* controller = Controller();

    if (controller == nullptr)
        return AAX_ERROR_NULL_OBJECT;

    auto effectId = AAX_CString {};
    auto checked = AAX_CheckedResult {};
    checked = controller->GetEffectID(&effectId);

    auto* description = findPlugin(described, bundleId, effectId.StdString());

    if (description == nullptr)
        return AAX_ERROR_NULL_OBJECT;

    auto input = AAX_EStemFormat {AAX_eStemFormat_None};
    auto output = AAX_EStemFormat {AAX_eStemFormat_None};
    auto rate = AAX_CSampleRate {44100.f};
    checked = controller->GetInputStemFormat(&input);
    checked = controller->GetOutputStemFormat(&output);
    checked = controller->GetSampleRate(&rate);

    auto wrapper = EA::makeOwned<PluginWrapper>(description->create(),
                                                PluginFormat::AAX,
                                                parameterIdentityFor(*description));
    auto layout = layoutFor(wrapper->busLayout(), {input, output});

    if (!layout || !wrapper->setLayout(*layout))
        return AAX_ERROR_NULL_COMPONENT;

    sampleRate = rate > 0.f ? static_cast<double>(rate) : 44100.0;
    wrapper->prepare(sampleRate, maxBlockSize);

    inputChannels = channelCount(input);
    outputChannels = channelCount(output);
    hasMidiIn = !layout->midiInputs.empty();
    hasMidiOut = !layout->midiOutputs.empty();

    inputTable.resize(inputChannels);
    outputTable.resize(outputChannels);
    incoming.reserveAtLeast(ProcessContext::defaultMidiCapacity);
    parser.setSysExBuffer(sysExBuffer);

    pluginWrapper = std::move(wrapper);
    pluginWrapper->plugin().setHostEditListener(this);
    registerParameters();

    auto latency = std::max(0, pluginWrapper->plugin().latencySamples());
    lastReportedLatency = latency;
    controller->SetSignalLatency(latency);
    return AAX_SUCCESS;
}

void Adapter::registerParameters()
{
    const auto& list = pluginWrapper->plugin().parameters();

    paramIds.resize(list.size());
    lastPluginValue = std::make_unique<std::atomic<float>[]>(
        static_cast<size_t>(std::max(1, list.size())));

    for (auto index = 0; index < list.size(); ++index)
    {
        lastPluginValue[static_cast<size_t>(index)] = list[index].getNormalized();

        if (!list.isRegistered(index))
            continue;

        registered.add(index);
        paramIds[index] = std::string(registeredId(list.entry(index).aaxId));
        mParameterManager.AddParameter(makeParameter(list.entry(index)));
    }

    byParamId = registered;
    std::sort(byParamId.begin(),
              byParamId.end(),
              [this](int a, int b) { return paramIds[a] < paramIds[b]; });
}

int Adapter::indexOf(AAX_CParamID id) const noexcept
{
    if (id == nullptr)
        return -1;

    auto key = std::string_view(id);
    auto found = std::lower_bound(byParamId.begin(),
                                  byParamId.end(),
                                  key,
                                  [this](int index, std::string_view wanted)
                                  { return paramIds[index] < wanted; });

    return found != byParamId.end() && paramIds[*found] == key ? *found : -1;
}

AAX_Result
    Adapter::ResetFieldData(AAX_CFieldIndex index, void* data, uint32_t size) const
{
    if (index != AAX_FIELD_INDEX(AlgorithmContext, privateData)
        || size != sizeof(PrivateData))
        return AAX_CEffectParameters::ResetFieldData(index, data, size);

    static_cast<PrivateData*>(data)->adapter = const_cast<Adapter*>(this);
    return AAX_SUCCESS;
}

AAX_Result Adapter::GetMasterBypassParameter(AAX_IString* id) const
{
    if (id == nullptr)
        return AAX_ERROR_NULL_ARGUMENT;

    auto index =
        pluginWrapper ? pluginWrapper->plugin().parameters().bypassIndex() : -1;
    id->Set(index >= 0 ? paramIds[index].c_str() : "");
    return AAX_SUCCESS;
}

AAX_Result Adapter::UpdateParameterNormalizedValue(AAX_CParamID id,
                                                   double value,
                                                   AAX_EUpdateSource source)
{
    auto result =
        AAX_CEffectParameters::UpdateParameterNormalizedValue(id, value, source);
    auto index = indexOf(id);

    if (result != AAX_SUCCESS || index < 0)
        return result;

    pluginWrapper->setNormalizedParameter(index, static_cast<float>(value));

    // The host already shows what it wrote, snapped or not.
    lastPluginValue[static_cast<size_t>(index)] =
        pluginWrapper->getNormalizedParameter(index);

    if (isMessageThread())
        notifyHostIfLatencyChanged();

    return result;
}

AAX_Result Adapter::GetNumberOfChunks(int32_t* count) const
{
    if (count == nullptr)
        return AAX_ERROR_NULL_ARGUMENT;

    *count = 1;
    return AAX_SUCCESS;
}

AAX_Result Adapter::GetChunkIDFromIndex(int32_t index, AAX_CTypeID* id) const
{
    if (id == nullptr)
        return AAX_ERROR_NULL_ARGUMENT;

    *id = index == 0 ? chunkId : 0;
    return index == 0 ? AAX_SUCCESS : AAX_ERROR_INVALID_CHUNK_INDEX;
}

AAX_Result Adapter::GetChunkSize(AAX_CTypeID id, uint32_t* size) const
{
    if (size == nullptr)
        return AAX_ERROR_NULL_ARGUMENT;

    *size = 0;

    if (id != chunkId)
        return AAX_ERROR_INVALID_CHUNK_ID;

    if (!pluginWrapper)
        return AAX_ERROR_NULL_OBJECT;

    return guarded(
        [&]
        {
            auto document = pluginWrapper->saveState(StateContext::Session);
            auto lock = std::lock_guard {chunkMutex};
            savedChunk = std::move(document);
            *size = static_cast<uint32_t>(savedChunk.size());
            return AAX_SUCCESS;
        });
}

AAX_Result Adapter::GetChunk(AAX_CTypeID id, AAX_SPlugInChunk* chunk) const
{
    if (chunk == nullptr)
        return AAX_ERROR_NULL_ARGUMENT;

    if (id != chunkId)
        return AAX_ERROR_INVALID_CHUNK_ID;

    auto lock = std::lock_guard {chunkMutex};

    if (!writeChunk(*chunk, savedChunk))
        return AAX_ERROR_INCORRECT_CHUNK_SIZE;

    chunk->fChunkID = chunkId;
    return AAX_SUCCESS;
}

AAX_Result Adapter::SetChunk(AAX_CTypeID id, const AAX_SPlugInChunk* chunk)
{
    if (chunk == nullptr)
        return AAX_ERROR_NULL_ARGUMENT;

    if (id != chunkId && id != juceChunkId && id != controlsChunkId)
        return AAX_ERROR_INVALID_CHUNK_ID;

    if (!pluginWrapper)
        return AAX_ERROR_NULL_OBJECT;

    return guarded(
        [&]() -> AAX_Result
        {
            if (id == chunkId)
            {
                pluginWrapper->loadState(readChunk(*chunk), StateContext::Session);
            }
            else if (id == juceChunkId)
            {
                // A JUCE build's state. Off the message thread it runs later,
                // and its parameterValuesChanged mirrors again then.
                auto foreign = ForeignState {};
                foreign.origin = ForeignOrigin::JuceAAX;
                foreign.bytes = readChunk(*chunk);
                foreign.context = StateContext::Session;
                pluginWrapper->loadForeignState(foreign);
            }
            else
            {
                // A JUCE build's controls: the SDK sets every parameter whose id
                // the chunk names, as JUCE's SetChunk leaves it to, and skips the
                // rest, so it lands under a JUCE IdScheme. Each value goes to the
                // host as a set request and comes back through
                // UpdateParameterNormalizedValue, perhaps later, so the AAX
                // parameters already hold what they should: nothing to mirror.
                auto result = AAX_CEffectParameters::SetChunk(id, chunk);

                if (result == AAX_SUCCESS)
                    notifyHostIfLatencyChanged();

                return result;
            }

            mirrorParametersToHost(true);
            notifyHostIfLatencyChanged();
            return AAX_SUCCESS;
        });
}

AAX_Result Adapter::CompareActiveChunk(const AAX_SPlugInChunk* chunk,
                                       AAX_CBoolean* isEqual) const
{
    if (chunk == nullptr || isEqual == nullptr)
        return AAX_ERROR_NULL_ARGUMENT;

    // An unknown chunk must not light the compare light, nor must a JUCE build's
    // 'juce' or 'elck', which a load reads but a save never writes.
    *isEqual = true;

    if (chunk->fChunkID != chunkId || !pluginWrapper)
        return AAX_SUCCESS;

    return guarded(
        [&]
        {
            const auto& list = pluginWrapper->plugin().parameters();
            auto bypass = list.bypassIndex();
            auto live = pluginWrapper->saveState(StateContext::Session);

            *isEqual = sameParameters(
                live, readChunk(*chunk), bypass >= 0 ? list.entry(bypass).id : "");
            return AAX_SUCCESS;
        });
}

AAX_Result Adapter::TimerWakeup()
{
    if (pluginWrapper)
    {
        notifyHostIfLatencyChanged();
        mirrorParametersToHost(false);
    }

    return AAX_CEffectParameters::TimerWakeup();
}

AAX_Result
    Adapter::NotificationReceived(AAX_CTypeID type, const void* data, uint32_t size)
{
    auto result = AAX_CEffectParameters::NotificationReceived(type, data, size);

    if (type != AAX_eNotificationEvent_TrackNameChanged || data == nullptr
        || !pluginWrapper)
        return result;

    try
    {
        const auto* name = static_cast<const AAX_IString*>(data)->Get();
        pluginWrapper->setTrackInfo(TrackInfo {
            .isValid = true, .name = name != nullptr ? name : std::string()});
    }
    catch (...)
    {
    }

    return result;
}

void Adapter::mirrorParametersToHost(bool all)
{
    for (auto index: registered)
    {
        auto value = pluginWrapper->getNormalizedParameter(index);
        auto& last = lastPluginValue[static_cast<size_t>(index)];

        if (!all && (value == last || pluginWrapper->isParameterHeld(index)))
            continue;

        last = value;

        if (auto* param = mParameterManager.GetParameterByID(paramIds[index].data());
            param != nullptr && param->GetNormalizedValue() != value)
            param->UpdateNormalizedValue(value);
    }
}

void Adapter::notifyHostIfLatencyChanged() noexcept
{
    auto& plugin = pluginWrapper->plugin();

    if (isMessageThread())
        pluginWrapper->takeLatencyChanged();

    auto reported = std::max(0, plugin.latencySamples());
    auto previous = lastReportedLatency.load();

    // Only the caller that moves the baseline tells the host.
    do
    {
        if (previous == reported)
            return;
    } while (!lastReportedLatency.compare_exchange_weak(previous, reported));

    if (auto* controller = Controller())
        controller->SetSignalLatency(reported);
}

void Adapter::render(AlgorithmContext& context) noexcept
{
    auto frames = context.numSamples != nullptr ? *context.numSamples : 0;

    if (context.outputs == nullptr || frames <= 0)
        return;

    if (!pluginWrapper)
    {
        for (auto channel = 0; channel < outputChannels; ++channel)
            std::fill_n(context.outputs[channel], frames, 0.f);

        return;
    }

    incoming.clear();

    if (hasMidiIn && context.midiIn != nullptr)
        if (const auto* stream = context.midiIn->GetNodeBuffer())
            readMidi(*stream,
                     parser,
                     timeAt(renderedSamples, sampleRate),
                     frames,
                     incoming);

    incoming.sortByOffset();

    auto playhead = Playhead {};

    if (context.transport != nullptr)
        if (const auto* transport = context.transport->GetTransport())
            playhead = readPlayhead(*transport);

    for (auto start = 0; start < frames; start += maxBlockSize)
        renderSlice(context,
                    advance(playhead, start, sampleRate),
                    start,
                    std::min(maxBlockSize, frames - start));

    renderedSamples += frames;
}

void Adapter::renderSlice(AlgorithmContext& context,
                          const Playhead& playhead,
                          int start,
                          int numSamples) noexcept
{
    auto& wrapper = *pluginWrapper;
    auto end = start + numSamples;

    wrapper.clearMidi();

    for (const auto& event: incoming)
    {
        if (event.sampleOffset < start || event.sampleOffset >= end)
            continue;

        auto shifted = event;
        shifted.sampleOffset -= start;
        wrapper.pushMidiIn(0, shifted);
    }

    wrapper.setPlayhead(playhead);
    wrapper.sortMidiInByOffset();

    auto hasInputs = inputChannels > 0 && context.inputs != nullptr;

    if (hasInputs)
    {
        for (auto channel = 0; channel < inputChannels; ++channel)
            inputTable[channel] = context.inputs[channel] + start;

        wrapper.bindInput(0, inputTable.data(), inputChannels, numSamples);
    }

    for (auto channel = 0; channel < outputChannels; ++channel)
        outputTable[channel] = context.outputs[channel] + start;

    wrapper.bindOutput(0,
                       outputTable.data(),
                       outputChannels,
                       numSamples,
                       hasInputs ? inputTable.data() : nullptr,
                       hasInputs ? inputChannels : 0);

    wrapper.process();

    if (!hasMidiOut || context.midiOut == nullptr || wrapper.midiOut().empty())
        return;

    for (const auto& event: wrapper.midiOut()[0])
        if (auto packet = packetFor(event, event.sampleOffset + start))
            context.midiOut->PostMIDIPacket(&*packet);
}

void Adapter::beginParameterEdit(int index) noexcept
{
    if (!pluginWrapper)
        return;

    pluginWrapper->holdParameter(index);

    if (pluginWrapper->plugin().parameters().isRegistered(index))
        TouchParameter(paramIds[index].data());
}

void Adapter::performParameterEdit(int index, float) noexcept
{
    if (!pluginWrapper)
        return;

    if (pluginWrapper->plugin().parameters().isRegistered(index))
    {
        auto value = pluginWrapper->getNormalizedParameter(index);
        lastPluginValue[static_cast<size_t>(index)] = value;
        SetParameterNormalizedValue(paramIds[index].data(), value);
    }

    notifyHostIfLatencyChanged();
}

void Adapter::endParameterEdit(int index) noexcept
{
    if (!pluginWrapper)
        return;

    if (pluginWrapper->plugin().parameters().isRegistered(index))
        ReleaseParameter(paramIds[index].data());

    pluginWrapper->releaseParameter(index);
}

void Adapter::latencyChanged() noexcept
{
    if (pluginWrapper)
        notifyHostIfLatencyChanged();
}

void Adapter::parameterValuesChanged() noexcept
{
    if (!pluginWrapper)
        return;

    try
    {
        mirrorParametersToHost(true);
    }
    catch (...)
    {
    }

    notifyHostIfLatencyChanged();
}

void Adapter::stateChanged() noexcept
{
    stateChanges.fetch_add(1, std::memory_order_relaxed);
}

AAX_Result Adapter::GetNumberOfChanges(int32_t* numChanges) const
{
    auto result = AAX_CEffectParameters::GetNumberOfChanges(numChanges);

    if (result == AAX_SUCCESS && numChanges != nullptr)
        *numChanges += stateChanges.load(std::memory_order_relaxed);

    return result;
}

void AAX_CALLBACK processProc(AlgorithmContext* const instancesBegin[],
                              const void* instancesEnd)
{
    for (auto* const* instance = instancesBegin; instance != instancesEnd;
         ++instance)
        if (auto* data = (*instance)->privateData; data != nullptr && data->adapter)
            data->adapter->render(**instance);
}

} // namespace MakeASound::AAX
