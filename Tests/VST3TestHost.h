#pragma once

// The VST3 adapter hosted in-process the way a DAW drives it, shared by the VST3
// suites: a component handler that records what the plugin reports, a stream
// that answers IStreamAttributes, and a fixture owning the host's buffers,
// parameter queues, event lists and process data.

#include <MakeASound/Plugin/VST3/VST3Common.h>
#include <MakeASound/Plugin/VST3/Adapter.h>
#include <MakeASound/Plugin/VST3/HostParameters.h>

#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/hosting/eventlist.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>
#include <pluginterfaces/vst/ivstattributes.h>
#include <pluginterfaces/vst/vstpresetkeys.h>

#include "TestPlugins.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace VST3Host
{
namespace Vst = Steinberg::Vst;
using Steinberg::int32;
using Steinberg::tresult;

// Stack-owned, so its reference count is a no-op.
struct TestComponentHandler final
    : Vst::IComponentHandler
    , Vst::IComponentHandler2
{
    enum class EditKind
    {
        Begin,
        Perform,
        End
    };

    struct Edit
    {
        EditKind kind = EditKind::Begin;
        Vst::ParamID id = 0;
        double value = 0.0;
    };

    tresult PLUGIN_API beginEdit(Vst::ParamID id) override
    {
        edits.push_back({EditKind::Begin, id, 0.0});
        return Steinberg::kResultOk;
    }

    tresult PLUGIN_API performEdit(Vst::ParamID id, Vst::ParamValue value) override
    {
        edits.push_back({EditKind::Perform, id, value});
        return Steinberg::kResultOk;
    }

    tresult PLUGIN_API endEdit(Vst::ParamID id) override
    {
        edits.push_back({EditKind::End, id, 0.0});
        return Steinberg::kResultOk;
    }

    tresult PLUGIN_API restartComponent(int32 flags) override
    {
        restarts.push_back(flags);
        return Steinberg::kResultOk;
    }

    tresult PLUGIN_API setDirty(Steinberg::TBool) override
    {
        return Steinberg::kResultOk;
    }

    tresult PLUGIN_API requestOpenEditor(Steinberg::FIDString) override
    {
        return Steinberg::kResultOk;
    }

    tresult PLUGIN_API startGroupEdit() override
    {
        ++groupStarts;
        return Steinberg::kResultOk;
    }

    tresult PLUGIN_API finishGroupEdit() override
    {
        ++groupEnds;
        return Steinberg::kResultOk;
    }

    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override
    {
        QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid, Vst::IComponentHandler)
        QUERY_INTERFACE(
            _iid, obj, Vst::IComponentHandler::iid, Vst::IComponentHandler)
        QUERY_INTERFACE(
            _iid, obj, Vst::IComponentHandler2::iid, Vst::IComponentHandler2)
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    int restartsWith(int32 flag) const
    {
        return static_cast<int>(std::count_if(restarts.begin(),
                                              restarts.end(),
                                              [flag](auto flags)
                                              { return (flags & flag) != 0; }));
    }

    std::vector<Edit> edits;
    std::vector<int32> restarts;
    int groupStarts = 0;
    int groupEnds = 0;
};

// A MemoryStream that tells the plugin which kind of state it carries.
struct TestAttributeStream final
    : Steinberg::MemoryStream
    , Vst::IStreamAttributes
    , Vst::IAttributeList
{
    explicit TestAttributeStream(std::string stateTypeToUse)
        : stateType(std::move(stateTypeToUse))
    {
    }

    tresult PLUGIN_API getFileName(Vst::String128) override
    {
        return Steinberg::kResultFalse;
    }

    Vst::IAttributeList* PLUGIN_API getAttributes() override { return this; }

    tresult PLUGIN_API setInt(AttrID, Steinberg::int64) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API getInt(AttrID, Steinberg::int64&) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API setFloat(AttrID, double) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API getFloat(AttrID, double&) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API setString(AttrID, const Vst::TChar*) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API getString(AttrID id,
                                 Vst::TChar* string,
                                 Steinberg::uint32 sizeInBytes) override
    {
        if (std::strcmp(id, Vst::PresetAttributes::kStateType) != 0)
            return Steinberg::kResultFalse;

        auto maxUnits = sizeInBytes / sizeof(Vst::TChar);

        if (stateType.size() + 1 > maxUnits)
            return Steinberg::kResultFalse;

        for (auto i = size_t {0}; i < stateType.size(); ++i)
            string[i] = static_cast<Vst::TChar>(stateType[i]);

        string[stateType.size()] = 0;
        return Steinberg::kResultTrue;
    }

    tresult PLUGIN_API setBinary(AttrID, const void*, Steinberg::uint32) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API getBinary(AttrID, const void*&, Steinberg::uint32&) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override
    {
        QUERY_INTERFACE(
            _iid, obj, Vst::IStreamAttributes::iid, Vst::IStreamAttributes)
        QUERY_INTERFACE(_iid, obj, Vst::IAttributeList::iid, Vst::IAttributeList)
        return Steinberg::MemoryStream::queryInterface(_iid, obj);
    }

    Steinberg::uint32 PLUGIN_API addRef() override
    {
        return Steinberg::MemoryStream::addRef();
    }

    Steinberg::uint32 PLUGIN_API release() override
    {
        return Steinberg::MemoryStream::release();
    }

    std::string stateType;
};

constexpr auto maxBlock = TestPlugins::blockSize;
constexpr auto maxEvents = 64;

// One bus of host memory: separate channel storage and the pointer table a
// host hands over.
struct HostBus
{
    explicit HostBus(int numChannels)
        : storage(static_cast<size_t>(numChannels), std::vector<float>(maxBlock))
    {
        for (auto& channel: storage)
            pointers.push_back(channel.data());
    }

    std::vector<std::vector<float>> storage;
    std::vector<float*> pointers;
};

// An adapter set up, activated and processing at 48 kHz in blocks of up to 64,
// with the host's side of a block ready to fill.
template <typename P>
struct AdapterHost
{
    AdapterHost()
        : adapter(
              Steinberg::owned(new MakeASound::VST3::Adapter(EA::makeOwned<P>())))
        , changes(4096)
        , inputEvents(maxEvents)
        , outputEvents(maxEvents)
    {
        adapter->initialize(nullptr);
        adapter->setComponentHandler(&handler);
        configure(48000.0, maxBlock);

        const auto& layout = adapter->wrapper().busLayout();

        for (const auto& bus: layout.inputs)
            inputs.emplace_back(bus.numChannels);

        for (const auto& bus: layout.outputs)
            outputs.emplace_back(bus.numChannels);

        inputBuses.resize(inputs.size());
        outputBuses.resize(outputs.size());
    }

    ~AdapterHost()
    {
        adapter->setProcessing(false);
        adapter->setActive(false);
        adapter->setComponentHandler(nullptr);
        adapter->terminate();
    }

    AdapterHost(const AdapterHost&) = delete;
    AdapterHost& operator=(const AdapterHost&) = delete;

    void configure(double sampleRate, int maxSamplesPerBlock)
    {
        adapter->setProcessing(false);
        adapter->setActive(false);

        auto setup = Vst::ProcessSetup {
            Vst::kRealtime, Vst::kSample32, maxSamplesPerBlock, sampleRate};
        adapter->setupProcessing(setup);
        adapter->setActive(true);
        adapter->setProcessing(true);
    }

    P& plugin() { return static_cast<P&>(adapter->plugin()); }

    Vst::ParamID hostIdOf(const MakeASound::Parameter& param)
    {
        const auto& list = adapter->plugin().parameters();
        return list.entry(list.indexOf(&param)).hostId;
    }

    void clearQueues()
    {
        changes.clearQueue();
        inputEvents.clear();
        outputEvents.clear();
    }

    void addPoint(Vst::ParamID id, int offset, double value)
    {
        auto queueIndex = int32 {};

        if (auto* queue = changes.addParameterData(id, queueIndex))
        {
            auto pointIndex = int32 {};
            queue->addPoint(offset, value, pointIndex);
        }
    }

    void addEvent(Vst::Event event) { inputEvents.addEvent(event); }

    // Points the process data at the host's buffers for a block of `numSamples`;
    // in place, each output bus reuses the input bus of its index.
    void fillBlock(int numSamples, bool inPlace = false)
    {
        for (auto i = size_t {0}; i < inputs.size(); ++i)
        {
            inputBuses[i] = {};
            inputBuses[i].numChannels =
                static_cast<int32>(inputs[i].pointers.size());
            inputBuses[i].channelBuffers32 = inputs[i].pointers.data();
        }

        for (auto i = size_t {0}; i < outputs.size(); ++i)
        {
            auto& source = inPlace && i < inputs.size() ? inputs[i] : outputs[i];
            outputBuses[i] = {};
            outputBuses[i].numChannels = static_cast<int32>(source.pointers.size());
            outputBuses[i].channelBuffers32 = source.pointers.data();
        }

        data = {};
        data.processMode = Vst::kRealtime;
        data.symbolicSampleSize = Vst::kSample32;
        data.numSamples = numSamples;
        data.numInputs = static_cast<int32>(inputBuses.size());
        data.numOutputs = static_cast<int32>(outputBuses.size());
        data.inputs = inputBuses.empty() ? nullptr : inputBuses.data();
        data.outputs = outputBuses.empty() ? nullptr : outputBuses.data();
        data.inputParameterChanges = &changes;
        data.inputEvents = &inputEvents;
        data.outputEvents = &outputEvents;
        data.processContext = &context;
    }

    tresult run() { return adapter->process(data); }

    TestComponentHandler handler;
    Steinberg::IPtr<MakeASound::VST3::Adapter> adapter;

    std::vector<HostBus> inputs;
    std::vector<HostBus> outputs;
    std::vector<Vst::AudioBusBuffers> inputBuses;
    std::vector<Vst::AudioBusBuffers> outputBuses;

    Vst::ParameterChanges changes;
    Vst::EventList inputEvents;
    Vst::EventList outputEvents;
    Vst::ProcessContext context {};
    Vst::ProcessData data {};
};

} // namespace VST3Host
