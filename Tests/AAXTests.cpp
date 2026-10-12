// The AAX adapter hosted in-process: the description GetEffectDescriptions fills,
// the category mapping, a whole block through the process procedure, parameters
// and their text, the edit gate and gestures, bypass, the state chunk, MIDI in and
// out, the transport and latency.

#include "AAXTestHost.h"

#include <MakeASound/Plugin/AAX/Conversion.h>
#include <MakeASound/Plugin/AAX/State.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <set>
#include <thread>

using namespace nano;
using namespace TestPlugins;
using namespace AAXHost;

namespace
{
using Module::ScopedStandIn;

bool near(double a, double b, double tolerance = 1e-4)
{
    return std::abs(a - b) < tolerance;
}

template <typename Predicate>
bool pumpUntil(Predicate ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {5000});
}

void moveEveryParameter(GainParams& params)
{
    params.gain.setValue(-12.f);
    params.filter.cutoff.setValue(440.f);
    params.mode.setValue(2.f);
    params.bypass.setValue(1.f);
    params.width.setValue(0.9f);
}

// Mono in, stereo out, the output left as bindOutput prepared it.
struct WidePlugin : Plugin
{
    std::string_view name() const override { return "Wide"; }

    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }

    bool acceptsLayout(const BusLayout& proposed) const override
    {
        return proposed.getMainInputChannels() == 1
               && proposed.getMainOutputChannels() == 2;
    }

    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}
};

struct MixedParams : ParameterGroup
{
    MixedParams() { add(shown, hidden, steps); }

    FloatParam shown {"Shown", 0.f, 1.f, 0.5f};
    FloatParam hidden {"Hidden", 0.f, 1.f, 0.5f, {.automatable = false}};
    ChoiceParam steps {"Steps", {"A", "B", "C", "D"}, 0};
};

struct MixedPlugin : StatePlugin<State<MixedParams>>
{
    std::string_view name() const override { return "Mixed"; }

    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}
};

struct LatencyParams : ParameterGroup
{
    LatencyParams() { add(latency); }

    FloatParam latency {"Latency", 0.f, 1000.f, 0.f};
};

struct LatencyPlugin : StatePlugin<State<LatencyParams>>
{
    std::string_view name() const override { return "Latency"; }

    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    int latencySamples() const noexcept override
    {
        return static_cast<int>(std::lround(params.latency.getValue()));
    }

    void announceLatency() { notifyHostLatencyChanged(); }
};

FakeCollection describeTestModule()
{
    auto collection = FakeCollection {};
    check(Format::describeEffects(
              collection, describeModule(), bundleId, "Test Package")
          == AAX_SUCCESS);
    return collection;
}

std::set<int> stemIndicesOf(const FakeEffect& effect, FourCC code)
{
    auto indices = std::set<int> {};

    for (auto* component: effect.components)
        indices.insert(static_cast<int>(
            uint32_t(component->property(AAX_eProperty_PlugInID_Native))
            - code.toUint32()));

    return indices;
}

auto tDescribe = test("AAX/describesAnEffectPerPlugin") = []
{
    auto collection = describeTestModule();
    check(collection.effects.size() == 2);
    check(collection.manufacturer == "MakeASound Tests");
    check(collection.packageNames == std::vector<std::string> {"Test Package"});

    auto* gain = collection.effect("com.makeasound.tests.Gain");
    auto* echo = collection.effect("com.makeasound.tests.Echo");
    check(gain != nullptr && echo != nullptr);
    check(gain->names.front() == "Gain");
    check(echo->names.front() == "Echo");
    check(gain->categories == AAX_EPlugInCategory_Effect);
    check(echo->categories == AAX_ePlugInCategory_SWGenerators);
    check(gain->procs.count(kAAX_ProcPtrID_Create_EffectParameters) == 1);
};

auto tStems = test("AAX/describesOnlyTheStemPairsThePluginAccepts") = []
{
    auto collection = describeTestModule();

    // The gain keeps its width; the instrument declares a stereo output.
    check(stemIndicesOf(*collection.effect("com.makeasound.tests.Gain"), "Gain")
          == std::set<int> {0, 2});
    check(stemIndicesOf(*collection.effect("com.makeasound.tests.Echo"), "Echo")
          == std::set<int> {4});

    for (auto* component: collection.effect("com.makeasound.tests.Gain")->components)
    {
        auto input = component->property(AAX_eProperty_InputStemFormat);
        check(input == component->property(AAX_eProperty_OutputStemFormat));
    }
};

auto tIds = test("AAX/theIdsAreTheModulesCodesAndUnique") = []
{
    auto collection = describeTestModule();
    auto natives = std::set<uint32_t> {};
    auto count = 0;

    for (const auto& [id, effect]: collection.effects)
    {
        for (auto* component: effect->components)
        {
            check(uint32_t(component->property(AAX_eProperty_ManufacturerID))
                  == FourCC("MaSo").toUint32());
            natives.insert(
                uint32_t(component->property(AAX_eProperty_PlugInID_Native)));
            ++count;
        }
    }

    check(uint32_t(collection.effect("com.makeasound.tests.Gain")
                       ->components.front()
                       ->property(AAX_eProperty_ProductID))
          == FourCC("Gain").toUint32());
    check(static_cast<int>(natives.size()) == count);
    check(Format::nativePluginId(describeModule().plugins[0], 2)
          == FourCC("Gain").toUint32() + 2);
};

auto tFields = test("AAX/theContextFieldsAreDescribed") = []
{
    auto collection = describeTestModule();
    auto field = [](const FakeComponent& component, AAX_CFieldIndex index)
    { return component.fields.at(index); };

    using Context = Format::AlgorithmContext;

    for (const auto& [id, effect]: collection.effects)
    {
        for (auto* component: effect->components)
        {
            check(component->processProc
                  == reinterpret_cast<void*>(&Format::processProc));
            check(field(*component, AAX_FIELD_INDEX(Context, inputs)).kind
                  == FieldKind::AudioIn);
            check(field(*component, AAX_FIELD_INDEX(Context, outputs)).kind
                  == FieldKind::AudioOut);
            check(field(*component, AAX_FIELD_INDEX(Context, numSamples)).kind
                  == FieldKind::BufferLength);

            auto data = field(*component, AAX_FIELD_INDEX(Context, privateData));
            check(data.kind == FieldKind::PrivateData);
            check(data.size == sizeof(Format::PrivateData));

            auto transport = field(*component, AAX_FIELD_INDEX(Context, transport));
            check(transport.kind == FieldKind::MidiNode);
            check(transport.nodeType == AAX_eMIDINodeType_Transport);
            check(component->property(AAX_eProperty_UsesTransport) == 1);
        }
    }

    auto* gain = collection.effect("com.makeasound.tests.Gain")->components.front();
    check(field(*gain, AAX_FIELD_INDEX(Context, midiIn)).kind
          == FieldKind::PrivateData);
    check(field(*gain, AAX_FIELD_INDEX(Context, midiOut)).kind
          == FieldKind::PrivateData);

    auto* echo = collection.effect("com.makeasound.tests.Echo")->components.front();
    auto in = field(*echo, AAX_FIELD_INDEX(Context, midiIn));
    auto out = field(*echo, AAX_FIELD_INDEX(Context, midiOut));
    check(in.kind == FieldKind::MidiNode
          && in.nodeType == AAX_eMIDINodeType_LocalInput);
    check(out.kind == FieldKind::MidiNode
          && out.nodeType == AAX_eMIDINodeType_LocalOutput);
};

auto tCategory = test("AAX/theCategoryDefaultsByKindUnlessNamed") = []
{
    auto plugin = PluginDescription {};

    plugin.category = Category::Effect;
    check(Format::categoryFor(plugin) == AAX_EPlugInCategory_Effect);

    plugin.category = Category::Instrument;
    check(Format::categoryFor(plugin) == AAX_ePlugInCategory_SWGenerators);

    plugin.category = Category::MidiEffect;
    check(Format::categoryFor(plugin) == AAX_ePlugInCategory_SWGenerators);

    plugin.aaxCategory = AAXCategory::Reverb;
    check(Format::categoryFor(plugin) == AAX_ePlugInCategory_Reverb);

    plugin.category = Category::Effect;
    plugin.aaxCategory = AAXCategory::Dynamics;
    check(Format::categoryFor(plugin) == AAX_ePlugInCategory_Dynamics);
};

auto tPrivateData = test("AAX/resetFieldDataPointsTheInstanceAtItsAdapter") = []
{
    auto host = AdapterHost<GainPlugin>("Gain");
    check(host.privateData.adapter == host.adapter.get());
    check(host.wrapper().plugin().format() == PluginFormat::AAX);
};

auto tBlock = test("AAX/aBlockRunsTheGainOutOfPlaceAndInPlace") = []
{
    for (auto inPlace: {false, true})
    {
        auto host = AdapterHost<GainPlugin>("Gain");

        if (inPlace)
            host.processInPlace();

        host.fillInputs(0.5f);
        host.plugin().params.gain.setValue(-6.0206f);
        host.render(blockSize);

        check(host.plugin().processed == 1);
        check(near(host.plugin().seenInput[0][3], 0.5f));
        check(near(host.output(0, 0), 0.25f, 1e-3));
        check(near(host.output(1, blockSize - 1), 0.25f, 1e-3));
    }
};

auto tWider = test("AAX/aChannelPastTheInputsWidthIsZeroed") = []
{
    auto wide = ScopedStandIn<WidePlugin>(Module::gainStandIn);
    auto host = AdapterHost<WidePlugin>(
        "Gain", AAX_eStemFormat_Mono, AAX_eStemFormat_Stereo);

    host.fillInputs(0.5f);
    host.fillOutputs(9.f);
    host.render(blockSize);

    check(near(host.output(0, 10), 0.5f));
    check(near(host.output(1, 10), 0.f));
};

auto tMono = test("AAX/aMonoGainRunsMono") = []
{
    auto host =
        AdapterHost<GainPlugin>("Gain", AAX_eStemFormat_Mono, AAX_eStemFormat_Mono);
    check(host.wrapper().busLayout().getMainInputChannels() == 1);

    host.fillInputs(0.5f);
    host.render(blockSize);
    check(near(host.output(0, 5), 0.5f));
};

auto tInstrument = test("AAX/anInstrumentRendersWithNoInput") = []
{
    auto host = AdapterHost<SynthPlugin>(
        "Echo", AAX_eStemFormat_None, AAX_eStemFormat_Stereo);
    check(host.context.inputs == nullptr);

    host.render(blockSize);
    check(near(host.output(0, 0), 0.5f));
    check(near(host.output(1, blockSize - 1), 0.5f));
};

auto tLongBlock = test("AAX/aBlockPastTheMaximumIsRenderedInSlices") = []
{
    auto host = AdapterHost<SynthPlugin>(
        "Echo", AAX_eStemFormat_None, AAX_eStemFormat_Stereo);

    host.fillOutputs(9.f);
    host.midiIn.add(1500, {0x90, 60, 100});
    host.render(2500);

    check(near(host.output(0, 0), 0.5f));
    check(near(host.output(1, 2499), 0.5f));

    // The last slice saw nothing; the echo left at its place in the host block.
    check(host.plugin().numEvents == 0);
    check(host.midiOut.numPosted == 1);
    check(host.midiOut.postedIs(0, 1500, {0x90, 60, 100}));
};

auto tParamIds = test("AAX/theParameterIdsAreTheHostIdsInHex") = []
{
    auto host = AdapterHost<GainPlugin>("Gain");
    const auto& list = host.plugin().parameters();

    auto count = int32_t {};
    host.adapter->GetNumberOfParameters(&count);
    check(count == list.size());

    for (auto i = 0; i < count; ++i)
    {
        auto id = AAX_CString {};
        check(host.adapter->GetParameterIDFromIndex(i, &id) == AAX_SUCCESS);
        check(id.StdString()
              == std::string(Format::paramIdFor(list.entry(i).hostId).data()));
        check(id.StdString().size() == 8);
        check(Format::hostIdOf(id.Get()) == list.entry(i).hostId);
    }

    check(Format::paramIdFor(0x0123abcd).data() == std::string("0123abcd"));
    check(!Format::hostIdOf("0123ABCD"));
    check(!Format::hostIdOf("0123abc"));
};

auto tParamInfo = test("AAX/onlyExposedParametersWithTheirSteps") = []
{
    auto mixed = ScopedStandIn<MixedPlugin>(Module::gainStandIn);
    auto host = AdapterHost<MixedPlugin>("Gain");
    const auto& params = host.plugin().params;

    auto count = int32_t {};
    host.adapter->GetNumberOfParameters(&count);
    check(count == 2);

    auto steps = int32_t {};
    auto type = AAX_EParameterType {};
    host.adapter->GetParameterNumberOfSteps(host.idOf(params.steps).data(), &steps);
    host.adapter->GetParameterType(host.idOf(params.steps).data(), &type);
    check(steps == 4 && type == AAX_eParameterType_Discrete);

    host.adapter->GetParameterType(host.idOf(params.shown).data(), &type);
    check(type == AAX_eParameterType_Continuous);

    auto automatable = AAX_CBoolean {};
    host.adapter->GetParameterIsAutomatable(host.idOf(params.shown).data(),
                                            &automatable);
    check(automatable);
    check(host.adapter->GetParameterIsAutomatable(host.idOf(params.hidden).data(),
                                                  &automatable)
          == AAX_ERROR_INVALID_PARAMETER_ID);

    auto name = AAX_CString {};
    host.adapter->GetParameterName(host.idOf(params.shown).data(), &name);
    check(name.StdString() == "Shown");
};

auto tParamText =
    test("AAX/theDisplayDelegateFormatsAndParsesThroughTheParameter") = []
{
    auto host = AdapterHost<GainPlugin>("Gain");
    const auto& params = host.plugin().params;
    auto gainId = host.idOf(params.gain);

    auto text = AAX_CString {};
    check(host.adapter->GetParameterStringFromValue(gainId.data(), 0.5, &text, 64)
          == AAX_SUCCESS);
    check(text.StdString() == params.gain.valueToText(params.gain.toPlain(0.5f)));

    auto value = 0.0;
    check(host.adapter->GetParameterValueFromString(
              host.idOf(params.mode).data(), &value, AAX_CString("Warm"))
          == AAX_SUCCESS);
    check(near(value, 0.5));

    auto minus6 = AAX_CString(params.gain.valueToText(-6.f).c_str());
    check(host.adapter->GetParameterValueFromString(gainId.data(), &value, minus6)
          == AAX_SUCCESS);
    check(near(params.gain.toPlain(static_cast<float>(value)), -6.f, 1e-2));

    // Text that yields no finite number keeps the value.
    host.plugin().params.gain.setValue(-20.f);

    for (auto* typed: {"nan", "inf"})
    {
        check(host.adapter->GetParameterValueFromString(
                  gainId.data(), &value, AAX_CString(typed))
              == AAX_SUCCESS);
        check(near(value, params.gain.getNormalized()));
    }
};

auto tHostWrite = test("AAX/aHostWriteLandsInTheNextBlock") = []
{
    auto host = AdapterHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;

    check(host.hostWrites(gain, gain.toNormalized(-6.0206f)) == AAX_SUCCESS);
    check(near(gain.getValue(), -6.0206f, 1e-3));
    check(near(host.hostValue(gain), gain.toNormalized(-6.0206f)));

    host.fillInputs(1.f);
    host.render(blockSize);
    check(near(host.output(0, 7), 0.5f, 1e-3));
};

auto tGesture = test("AAX/aGestureTouchesSetsAndReleasesAndHoldsTheParameter") = []
{
    auto host = AdapterHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;
    auto& listener = *host.plugin().hostEditListener();
    auto index = host.plugin().parameters().indexOf(&gain);
    auto id = host.idOf(gain);
    auto& edits = host.controller.edits;

    listener.beginParameterEdit(index);
    check(!edits.empty() && edits.back().is(EditKind::Touch, id));

    // The host's own write is dropped while the plugin holds the parameter.
    host.controller.echo = false;
    host.hostWrites(gain, gain.toNormalized(-30.f));
    check(near(gain.getValue(), 0.f));
    host.controller.echo = true;

    gain.setValue(-12.f);
    edits.clear();
    listener.performParameterEdit(index, gain.getNormalized());

    auto set = std::find_if(edits.begin(),
                            edits.end(),
                            [](const Edit& edit)
                            { return edit.kind == EditKind::SetValue; });
    check(set != edits.end() && set->is(EditKind::SetValue, id));
    check(near(set->value, gain.toNormalized(-12.f)));
    check(near(host.hostValue(gain), gain.toNormalized(-12.f)));
    check(near(gain.getValue(), -12.f));

    listener.endParameterEdit(index);
    check(edits.back().is(EditKind::Release, id));

    host.hostWrites(gain, gain.toNormalized(-30.f));
    check(near(gain.getValue(), -30.f, 1e-3));
};

auto tBypass = test("AAX/theMasterBypassIsTheBypassParameter") = []
{
    {
        auto host = AdapterHost<GainPlugin>("Gain");
        auto id = AAX_CString {};
        check(host.adapter->GetMasterBypassParameter(&id) == AAX_SUCCESS);
        check(id.StdString()
              == std::string(host.idOf(host.plugin().params.bypass).data()));

        host.hostWrites(host.plugin().params.bypass, 1.0);
        check(host.plugin().params.bypass.isOn());
    }

    auto host = AdapterHost<SynthPlugin>(
        "Echo", AAX_eStemFormat_None, AAX_eStemFormat_Stereo);
    auto id = AAX_CString("x");
    check(host.adapter->GetMasterBypassParameter(&id) == AAX_SUCCESS);
    check(id.StdString().empty());
};

struct Chunk
{
    std::vector<char> storage;

    AAX_SPlugInChunk& get()
    {
        return *reinterpret_cast<AAX_SPlugInChunk*>(storage.data());
    }
};

template <typename P>
Chunk saveChunk(AdapterHost<P>& host)
{
    auto id = AAX_CTypeID {};
    check(host.adapter->GetChunkIDFromIndex(0, &id) == AAX_SUCCESS);
    check(id == Format::chunkId);

    auto size = uint32_t {};
    check(host.adapter->GetChunkSize(id, &size) == AAX_SUCCESS);
    check(size > 0);

    auto chunk = Chunk {};
    chunk.storage.resize(sizeof(AAX_SPlugInChunk) + size);
    chunk.get().fSize = static_cast<int32_t>(size);
    check(host.adapter->GetChunk(id, &chunk.get()) == AAX_SUCCESS);
    check(chunk.get().fSize == static_cast<int32_t>(size));
    check(chunk.get().fChunkID == Format::chunkId);
    return chunk;
}

auto tState = test("AAX/theChunkRoundTripsTheState") = []
{
    auto count = int32_t {};

    auto source = AdapterHost<GainPlugin>("Gain");
    source.adapter->GetNumberOfChunks(&count);
    check(count == 1);

    moveEveryParameter(source.plugin().params);
    source.plugin().state.preset = "Saved";
    source.plugin().state.markChanged();
    pumpUntil([&] { return source.plugin().isStateSnapshotCurrent(); });

    auto chunk = saveChunk(source);

    auto target = AdapterHost<GainPlugin>("Gain");
    check(target.adapter->SetChunk(Format::chunkId, &chunk.get()) == AAX_SUCCESS);

    const auto& params = target.plugin().params;
    check(near(params.gain.getValue(), -12.f));
    check(near(params.filter.cutoff.getValue(), 440.f, 1e-2));
    check(params.mode.getIndex() == 2);
    check(params.bypass.isOn());
    check(near(params.width.getValue(), 0.9f));
    check(pumpUntil([&] { return target.plugin().state.preset == "Saved"; }));

    // The host's view of each value follows the load.
    check(near(target.hostValue(params.gain), params.gain.getNormalized()));
    check(near(target.hostValue(params.mode), 1.0));

    check(target.adapter->SetChunk('elck', &chunk.get())
          == AAX_ERROR_INVALID_CHUNK_ID);
};

auto tStateInserted = test("AAX/aParameterInsertedMidListKeepsTheOthers") = []
{
    auto source = AdapterHost<GainPlugin>("Gain");
    moveEveryParameter(source.plugin().params);
    auto chunk = saveChunk(source);

    auto newer = ScopedStandIn<NewerGainPlugin>(Module::gainStandIn);
    auto target = AdapterHost<NewerGainPlugin>("Gain");
    check(target.adapter->SetChunk(Format::chunkId, &chunk.get()) == AAX_SUCCESS);

    const auto& params = target.plugin().params;
    check(near(params.gain.getValue(), -12.f));
    check(near(params.drive.getValue(), 0.25f));
    check(params.mode.getIndex() == 2);
    check(params.bypass.isOn());
    check(near(target.hostValue(params.drive), params.drive.getNormalized()));
};

auto tSaveOffThread = test("AAX/aSaveFromAWorkerReturnsWhileTheMainThreadSpins") = []
{
    auto host = AdapterHost<GainPlugin>("Gain");
    host.plugin().params.gain.setValue(-6.f);

    auto chunk = Chunk {};
    auto result = AAX_Result {AAX_ERROR_UNIMPLEMENTED};
    auto done = std::atomic<bool> {false};

    auto saver = std::thread(
        [&]
        {
            auto size = uint32_t {};
            result = host.adapter->GetChunkSize(Format::chunkId, &size);

            if (result == AAX_SUCCESS)
            {
                chunk.storage.resize(sizeof(AAX_SPlugInChunk) + size);
                chunk.get().fSize = static_cast<int32_t>(size);
                result = host.adapter->GetChunk(Format::chunkId, &chunk.get());
            }

            done = true;
        });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (!done && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    check(done.load());
    saver.join();
    check(result == AAX_SUCCESS);

    auto target = AdapterHost<GainPlugin>("Gain");
    check(target.adapter->SetChunk(Format::chunkId, &chunk.get()) == AAX_SUCCESS);
    check(near(target.plugin().params.gain.getValue(), -6.f));
};

auto tCompare = test("AAX/compareActiveChunkComparesTheParameters") = []
{
    auto host = AdapterHost<GainPlugin>("Gain");
    auto chunk = saveChunk(host);
    auto equal = AAX_CBoolean {false};

    check(host.adapter->CompareActiveChunk(&chunk.get(), &equal) == AAX_SUCCESS);
    check(equal);

    // The master bypass is left out of the compare, as the SDK leaves it.
    host.plugin().params.bypass.setValue(1.f);
    host.adapter->CompareActiveChunk(&chunk.get(), &equal);
    check(equal);

    host.plugin().params.gain.setValue(-3.f);
    host.adapter->CompareActiveChunk(&chunk.get(), &equal);
    check(!equal);

    chunk.get().fChunkID = 'elck';
    host.adapter->CompareActiveChunk(&chunk.get(), &equal);
    check(equal);
};

auto tMidiIn = test("AAX/notesArriveAtTheirTimestamps") = []
{
    auto host = AdapterHost<SynthPlugin>(
        "Echo", AAX_eStemFormat_None, AAX_eStemFormat_Stereo);

    host.midiIn.add(40, {0x80, 60, 0});
    host.midiIn.add(10, {0x91, 60, 127});
    host.midiIn.add(20, {0xb1, 123, 0});
    host.render(blockSize);

    const auto& plugin = host.plugin();
    check(plugin.numEvents == 3);
    check(plugin.events[0].isNoteOn() && plugin.events[0].sampleOffset == 10);
    check(plugin.events[0].channel == 1);
    check(plugin.events[1].isControlChange() && plugin.events[1].sampleOffset == 20);
    check(plugin.events[2].isNoteOff() && plugin.events[2].sampleOffset == 40);

    // Past the block it lands on the last sample.
    host.midiIn.clear();
    host.midiIn.add(500, {0x90, 61, 1});
    host.render(blockSize);
    check(plugin.numEvents == 1 && plugin.events[0].sampleOffset == blockSize - 1);
};

auto tSysEx = test("AAX/aSysExSplitAcrossPacketsIsAssembled") = []
{
    auto host = AdapterHost<SynthPlugin>(
        "Echo", AAX_eStemFormat_None, AAX_eStemFormat_Stereo);

    host.midiIn.add(5, {0xf0, 0x7e, 0x7f, 0x09});
    host.midiIn.add(5, {0x01, 0xf7});
    host.render(blockSize);

    const auto& plugin = host.plugin();
    check(plugin.numEvents == 1);

    const auto* sysEx = plugin.events[0].asSysEx();
    check(sysEx != nullptr && sysEx->size == 6);
    check(sysEx->data[0] == 0xf0 && sysEx->data[4] == 0x01
          && sysEx->data[5] == 0xf7);

    // The plugin echoed it, but only channel-voice messages leave.
    check(host.midiOut.numPosted == 0);
};

auto tMidiOut = test("AAX/theEchoLeavesThroughTheOutputNode") = []
{
    auto host = AdapterHost<SynthPlugin>(
        "Echo", AAX_eStemFormat_None, AAX_eStemFormat_Stereo);

    host.midiIn.add(3, {0x90, 64, 90});
    host.midiIn.add(30, {0xe0, 0, 0x50});
    host.midiIn.add(33, {0x80, 64, 0});
    host.render(blockSize);

    check(host.midiOut.numPosted == 3);
    check(host.midiOut.postedIs(0, 3, {0x90, 64, 90}));
    check(host.midiOut.postedIs(1, 30, {0xe0, 0, 0x50}));
    check(host.midiOut.postedIs(2, 33, {0x80, 64, 0}));
};

auto tNoMidiNodes = test("AAX/aLayoutWithNoMidiLeavesTheNodesAlone") = []
{
    auto host = AdapterHost<GainPlugin>("Gain");
    host.midiIn.add(3, {0x90, 64, 90});
    host.render(blockSize);
    check(host.midiOut.numPosted == 0);
};

auto tPlayhead = test("AAX/thePlayheadComesFromTheTransport") = []
{
    auto host = AdapterHost<GainPlugin>("Gain");
    auto& transport = host.transport;
    transport.tempo = 90.0;
    transport.meterNumerator = 3;
    transport.meterDenominator = 8;
    transport.playing = true;
    transport.sample = 48000;
    transport.ticks = static_cast<int64_t>(8 * Format::ticksPerQuarter);
    transport.looping = true;
    transport.loopStart = static_cast<int64_t>(4 * Format::ticksPerQuarter);
    transport.loopEnd = static_cast<int64_t>(12 * Format::ticksPerQuarter);
    host.render(blockSize);

    auto playhead = host.plugin().playhead;
    check(playhead.isValid && playhead.isPlaying && playhead.isLooping);
    check(near(playhead.bpm, 90.0));
    check(playhead.timeSignature.numerator == 3);
    check(playhead.timeSignature.denominator == 8);
    check(playhead.sampleTime == 48000);
    check(near(playhead.ppqPosition, 8.0));
    check(near(playhead.loopStartPpq, 4.0) && near(playhead.loopEndPpq, 12.0));

    transport.answers = false;
    host.render(blockSize);
    check(!host.plugin().playhead.isValid);

    // A later slice of a long block is further on.
    auto later = Format::advance(playhead, 24000, 48000.0);
    check(later.sampleTime == 72000);
    check(near(later.ppqPosition, 8.75));
};

auto tLatency = test("AAX/aLatencyChangeReachesSetSignalLatency") = []
{
    auto standIn = ScopedStandIn<LatencyPlugin>(Module::gainStandIn);
    auto host = AdapterHost<LatencyPlugin>("Gain");
    auto& latency = host.plugin().params.latency;

    check(host.controller.latency == 0);
    auto calls = host.controller.latencyCalls;

    host.hostWrites(latency, latency.toNormalized(256.f));
    check(host.controller.latency == 256);
    check(host.controller.latencyCalls == calls + 1);

    host.hostWrites(latency, latency.toNormalized(256.f));
    check(host.controller.latencyCalls == calls + 1);

    latency.setValue(64.f);
    host.plugin().announceLatency();
    check(host.controller.latency == 64);

    latency.setValue(32.f);
    check(host.adapter->TimerWakeup() == AAX_SUCCESS);
    check(host.controller.latency == 32);
};

auto tMirror = test("AAX/aValueThePluginMovesReachesTheHostOnTheTimer") = []
{
    auto host = AdapterHost<GainPlugin>("Gain");
    auto& gain = host.plugin().params.gain;

    gain.setValue(-24.f);
    check(!near(host.hostValue(gain), gain.getNormalized()));

    host.adapter->TimerWakeup();
    check(near(host.hostValue(gain), gain.getNormalized()));

    // A host value the plugin snapped is left as the host wrote it.
    auto& mode = host.plugin().params.mode;
    host.hostWrites(mode, 0.4);
    host.adapter->TimerWakeup();
    check(near(host.hostValue(mode), 0.4));
};
} // namespace
