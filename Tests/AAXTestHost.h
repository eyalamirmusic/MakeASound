#pragma once

// The AAX adapter hosted in-process, shared by the AAX suites: recording fakes of
// the description interfaces GetEffectDescriptions fills, a controller that is also
// the automation delegate, MIDI nodes and a transport for the render context, and
// a fixture owning one initialized adapter, its channel memory and its context.

#include "JuceFixtures.h"
#include "TestPlugins.h"

#include <MakeASound/Plugin/AAX/Adapter.h>
#include <MakeASound/Plugin/AAX/AlgorithmContext.h>
#include <MakeASound/Plugin/AAX/Describe.h>
#include <MakeASound/Plugin/AAX/Parameters.h>

#include <AAX_CString.h>
#include <AAX_IACFAutomationDelegate.h>
#include <AAX_IACFController.h>
#include <AAX_UIDs.h>
#include <acfresult.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <cstring>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace AAXHost
{
using namespace MakeASound;
namespace Format = MakeASound::AAX;

constexpr auto sampleRate = 48000.0;
constexpr auto bufferCapacity = 4096;
constexpr auto bundleId = "com.makeasound.tests";

// ---------------------------------------------------------------- description

struct FakePropertyMap : AAX_IPropertyMap
{
    AAX_CBoolean GetProperty(AAX_EProperty property,
                             AAX_CPropertyValue* value) const override
    {
        auto found = values.find(property);

        if (found == values.end())
            return false;

        *value = found->second;
        return true;
    }

    AAX_CBoolean GetPointerProperty(AAX_EProperty, const void**) const override
    {
        return false;
    }

    AAX_Result AddProperty(AAX_EProperty property, AAX_CPropertyValue value) override
    {
        values[property] = value;
        return AAX_SUCCESS;
    }

    AAX_Result AddPointerProperty(AAX_EProperty, const void*) override
    {
        return AAX_SUCCESS;
    }

    AAX_Result AddPointerProperty(AAX_EProperty, const char*) override
    {
        return AAX_SUCCESS;
    }

    AAX_Result RemoveProperty(AAX_EProperty property) override
    {
        values.erase(property);
        return AAX_SUCCESS;
    }

    AAX_Result AddPropertyWithIDArray(AAX_EProperty,
                                      const AAX_SPlugInIdentifierTriad*,
                                      uint32_t) override
    {
        return AAX_SUCCESS;
    }

    AAX_CBoolean GetPropertyWithIDArray(AAX_EProperty,
                                        const AAX_SPlugInIdentifierTriad**,
                                        uint32_t*) const override
    {
        return false;
    }

    IACFUnknown* GetIUnknown() override { return nullptr; }

    std::map<AAX_EProperty, AAX_CPropertyValue> values;
};

enum class FieldKind
{
    AudioIn,
    AudioOut,
    BufferLength,
    Clock,
    PrivateData,
    MidiNode,
    Other
};

struct Field
{
    FieldKind kind = FieldKind::Other;
    int32_t size = 0;
    AAX_EMIDINodeType nodeType = AAX_eMIDINodeType_LocalInput;
};

struct FakeComponent : AAX_IComponentDescriptor
{
    AAX_Result Clear() override { return AAX_SUCCESS; }

    AAX_Result AddAudioIn(AAX_CFieldIndex index) override
    {
        return add(index, {FieldKind::AudioIn});
    }

    AAX_Result AddAudioOut(AAX_CFieldIndex index) override
    {
        return add(index, {FieldKind::AudioOut});
    }

    AAX_Result AddAudioBufferLength(AAX_CFieldIndex index) override
    {
        return add(index, {FieldKind::BufferLength});
    }

    AAX_Result AddSampleRate(AAX_CFieldIndex index) override
    {
        return add(index, {});
    }

    AAX_Result AddClock(AAX_CFieldIndex index) override
    {
        return add(index, {FieldKind::Clock});
    }

    AAX_Result AddSideChainIn(AAX_CFieldIndex index) override
    {
        return add(index, {});
    }

    AAX_Result
        AddDataInPort(AAX_CFieldIndex index, uint32_t, AAX_EDataInPortType) override
    {
        return add(index, {});
    }

    AAX_Result AddAuxOutputStem(AAX_CFieldIndex, int32_t, const char[]) override
    {
        return AAX_SUCCESS;
    }

    AAX_Result AddPrivateData(AAX_CFieldIndex index, int32_t size, uint32_t) override
    {
        return add(index, {FieldKind::PrivateData, size});
    }

    AAX_Result AddTemporaryData(AAX_CFieldIndex index, uint32_t) override
    {
        return add(index, {});
    }

    AAX_Result AddDmaInstance(AAX_CFieldIndex index, AAX_IDma::EMode) override
    {
        return add(index, {});
    }

    AAX_Result
        AddMeters(AAX_CFieldIndex index, const AAX_CTypeID*, uint32_t) override
    {
        return add(index, {});
    }

    AAX_Result AddMIDINode(AAX_CFieldIndex index,
                           AAX_EMIDINodeType type,
                           const char[],
                           uint32_t) override
    {
        return add(index, {FieldKind::MidiNode, 0, type});
    }

    AAX_Result AddReservedField(AAX_CFieldIndex index, uint32_t) override
    {
        return add(index, {});
    }

    AAX_IPropertyMap* NewPropertyMap() const override
    {
        maps.push_back(std::make_unique<FakePropertyMap>());
        return maps.back().get();
    }

    AAX_IPropertyMap* DuplicatePropertyMap(AAX_IPropertyMap*) const override
    {
        return NewPropertyMap();
    }

    AAX_Result AddProcessProc_Native(AAX_CProcessProc proc,
                                     AAX_IPropertyMap* properties,
                                     AAX_CInstanceInitProc,
                                     AAX_CBackgroundProc,
                                     AAX_CSelector*) override
    {
        processProc = reinterpret_cast<void*>(proc);
        this->properties = static_cast<FakePropertyMap*>(properties);
        return AAX_SUCCESS;
    }

    AAX_Result AddProcessProc_TI(const char[],
                                 const char[],
                                 AAX_IPropertyMap*,
                                 const char[],
                                 const char[],
                                 AAX_CSelector*) override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result AddProcessProc(AAX_IPropertyMap*, AAX_CSelector*, int32_t) override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result add(AAX_CFieldIndex index, Field field)
    {
        fields[index] = field;
        return AAX_SUCCESS;
    }

    int32_t property(AAX_EProperty id) const
    {
        auto value = AAX_CPropertyValue {-1};
        nano::check(properties != nullptr && properties->GetProperty(id, &value));
        return value;
    }

    std::map<AAX_CFieldIndex, Field> fields;
    mutable std::vector<std::unique_ptr<FakePropertyMap>> maps;
    FakePropertyMap* properties = nullptr;
    void* processProc = nullptr;
};

struct FakeEffect : AAX_IEffectDescriptor
{
    AAX_IComponentDescriptor* NewComponentDescriptor() override
    {
        created.push_back(std::make_unique<FakeComponent>());
        return created.back().get();
    }

    AAX_Result AddComponent(AAX_IComponentDescriptor* component) override
    {
        components.push_back(static_cast<FakeComponent*>(component));
        return AAX_SUCCESS;
    }

    AAX_Result AddName(const char* name) override
    {
        names.emplace_back(name);
        return AAX_SUCCESS;
    }

    AAX_Result AddCategory(uint32_t category) override
    {
        categories |= category;
        return AAX_SUCCESS;
    }

    AAX_Result AddCategoryBypassParameter(uint32_t, AAX_CParamID) override
    {
        return AAX_SUCCESS;
    }

    AAX_Result AddProcPtr(void* proc, AAX_CProcPtrID id) override
    {
        procs[id] = proc;
        return AAX_SUCCESS;
    }

    AAX_IPropertyMap* NewPropertyMap() override
    {
        maps.push_back(std::make_unique<FakePropertyMap>());
        return maps.back().get();
    }

    AAX_Result SetProperties(AAX_IPropertyMap*) override { return AAX_SUCCESS; }

    AAX_Result AddResourceInfo(AAX_EResourceType, const char*) override
    {
        return AAX_SUCCESS;
    }

    AAX_Result
        AddMeterDescription(AAX_CTypeID, const char*, AAX_IPropertyMap*) override
    {
        return AAX_SUCCESS;
    }

    AAX_Result AddControlMIDINode(AAX_CTypeID,
                                  AAX_EMIDINodeType,
                                  const char[],
                                  uint32_t) override
    {
        return AAX_SUCCESS;
    }

    std::vector<std::unique_ptr<FakeComponent>> created;
    std::vector<FakeComponent*> components;
    std::vector<std::string> names;
    uint32_t categories = 0;
    std::map<AAX_CProcPtrID, void*> procs;
    std::vector<std::unique_ptr<FakePropertyMap>> maps;
};

struct FakeCollection : AAX_ICollection
{
    AAX_IEffectDescriptor* NewDescriptor() override
    {
        created.push_back(std::make_unique<FakeEffect>());
        return created.back().get();
    }

    AAX_Result AddEffect(const char* id, AAX_IEffectDescriptor* effect) override
    {
        effects.emplace_back(id, static_cast<FakeEffect*>(effect));
        return AAX_SUCCESS;
    }

    AAX_Result SetManufacturerName(const char* name) override
    {
        manufacturer = name;
        return AAX_SUCCESS;
    }

    AAX_Result AddPackageName(const char* name) override
    {
        packageNames.emplace_back(name);
        return AAX_SUCCESS;
    }

    AAX_Result SetPackageVersion(uint32_t version) override
    {
        packageVersion = version;
        return AAX_SUCCESS;
    }

    AAX_IPropertyMap* NewPropertyMap() override
    {
        maps.push_back(std::make_unique<FakePropertyMap>());
        return maps.back().get();
    }

    AAX_Result SetProperties(AAX_IPropertyMap*) override { return AAX_SUCCESS; }

    AAX_Result GetHostVersion(uint32_t* version) const override
    {
        *version = 0;
        return AAX_SUCCESS;
    }

    AAX_IDescriptionHost* DescriptionHost() override { return nullptr; }
    const AAX_IDescriptionHost* DescriptionHost() const override { return nullptr; }
    IACFDefinition* HostDefinition() const override { return nullptr; }

    FakeEffect* effect(const std::string& id) const
    {
        for (const auto& [name, effect]: effects)
            if (name == id)
                return effect;

        return nullptr;
    }

    std::vector<std::unique_ptr<FakeEffect>> created;
    std::vector<std::pair<std::string, FakeEffect*>> effects;
    std::string manufacturer;
    std::vector<std::string> packageNames;
    uint32_t packageVersion = 0;
    std::vector<std::unique_ptr<FakePropertyMap>> maps;
};

// ---------------------------------------------------------------- render context

struct FakeTransport : AAX_ITransport
{
    AAX_Result answer() const
    {
        return answers ? AAX_SUCCESS : AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetCurrentTempo(double* value) const override
    {
        *value = tempo;
        return answer();
    }

    AAX_Result GetCurrentMeter(int32_t* numerator,
                               int32_t* denominator) const override
    {
        *numerator = meterNumerator;
        *denominator = meterDenominator;
        return answer();
    }

    AAX_Result IsTransportPlaying(bool* value) const override
    {
        *value = playing;
        return answer();
    }

    AAX_Result GetCurrentTickPosition(int64_t* value) const override
    {
        *value = ticks;
        return answer();
    }

    AAX_Result GetCurrentLoopPosition(bool* isLooping,
                                      int64_t* start,
                                      int64_t* end) const override
    {
        *isLooping = looping;
        *start = loopStart;
        *end = loopEnd;
        return answer();
    }

    AAX_Result GetCurrentNativeSampleLocation(int64_t* value) const override
    {
        *value = sample;
        return answer();
    }

    AAX_Result GetCustomTickPosition(int64_t*, int64_t) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result
        GetBarBeatPosition(int32_t*, int32_t*, int64_t*, int64_t) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetTicksPerQuarter(uint32_t*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetCurrentTicksPerBeat(uint32_t*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetTimelineSelectionStartPosition(int64_t*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetTimeCodeInfo(AAX_EFrameRate*, int32_t*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetFeetFramesInfo(AAX_EFeetFramesRate*, int64_t*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result IsMetronomeEnabled(int32_t*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetHDTimeCodeInfo(AAX_EFrameRate*, int64_t*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result RequestTransportStart() override { return AAX_ERROR_UNIMPLEMENTED; }
    AAX_Result RequestTransportStop() override { return AAX_ERROR_UNIMPLEMENTED; }

    AAX_Result GetTimelineSelectionEndPosition(int64_t*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetKeySignature(int64_t, uint32_t*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    bool answers = true;
    double tempo = 120.0;
    int32_t meterNumerator = 4;
    int32_t meterDenominator = 4;
    bool playing = false;
    int64_t ticks = 0;
    int64_t sample = 0;
    bool looping = false;
    int64_t loopStart = 0;
    int64_t loopEnd = 0;
};

// A node's buffer, filled before a block, and what the plugin posted to it, both
// in fixed storage so a block never allocates on the host's side.
struct FakeMidiNode : AAX_IMIDINode
{
    static constexpr int capacity = 256;

    AAX_CMidiStream* GetNodeBuffer() override
    {
        stream.mBuffer = incoming.data();
        return &stream;
    }

    AAX_Result PostMIDIPacket(AAX_CMidiPacket* packet) override
    {
        if (numPosted < capacity)
            posted[static_cast<size_t>(numPosted++)] = *packet;

        return AAX_SUCCESS;
    }

    AAX_ITransport* GetTransport() override { return transport; }

    void add(uint32_t timestamp, std::initializer_list<int> bytes)
    {
        auto& packet = incoming[stream.mBufferSize++];
        packet = AAX_CMidiPacket {};
        packet.mTimestamp = timestamp;
        packet.mLength = static_cast<uint32_t>(bytes.size());

        auto i = 0;

        for (auto byte: bytes)
            packet.mData[i++] = static_cast<unsigned char>(byte);
    }

    void clear()
    {
        stream.mBufferSize = 0;
        numPosted = 0;
    }

    bool postedIs(int index,
                  uint32_t timestamp,
                  std::initializer_list<int> bytes) const
    {
        const auto& packet = posted[static_cast<size_t>(index)];

        if (packet.mTimestamp != timestamp || packet.mLength != bytes.size())
            return false;

        auto i = 0;

        for (auto byte: bytes)
            if (packet.mData[i++] != byte)
                return false;

        return true;
    }

    std::array<AAX_CMidiPacket, capacity> incoming {};
    AAX_CMidiStream stream {0, nullptr};
    std::array<AAX_CMidiPacket, capacity> posted {};
    int numPosted = 0;
    AAX_ITransport* transport = nullptr;
};

// ---------------------------------------------------------------- controller

enum class EditKind
{
    Touch,
    SetValue,
    Release
};

struct Edit
{
    EditKind kind = EditKind::Touch;
    std::array<char, kAAX_ParameterIdentifierMaxSize> id {};
    double value = 0.0;

    bool is(EditKind expected, std::string_view expectedId) const
    {
        return kind == expected && std::string_view(id.data()) == expectedId;
    }
};

// What the host is to the data model: its controller and its automation delegate.
// A set-value request echoes back through UpdateParameterNormalizedValue, as Pro
// Tools answers one. Every record is fixed storage, so a gesture never allocates
// here.
struct FakeController
    : AAX_IACFController_V2
    , AAX_IACFAutomationDelegate
{
    FakeController() { edits.reserve(4096); }

    IACFUnknown* unknown() { return static_cast<AAX_IACFController*>(this); }

    ACFRESULT ACFMETHODCALLTYPE QueryInterface(const acfIID& iid,
                                               void** out) override
    {
        auto is = [&](const acfIID& other)
        { return std::memcmp(&iid, &other, sizeof(acfIID)) == 0; };

        if (is(IID_IAAXControllerV1) || is(IID_IAAXControllerV2))
            *out = static_cast<AAX_IACFController_V2*>(this);
        else if (is(IID_IAAXAutomationDelegateV1))
            *out = static_cast<AAX_IACFAutomationDelegate*>(this);
        else
        {
            *out = nullptr;
            return ACF_E_NOINTERFACE;
        }

        return ACF_OK;
    }

    acfUInt32 ACFMETHODCALLTYPE AddRef() override { return 1; }
    acfUInt32 ACFMETHODCALLTYPE Release() override { return 1; }

    // AAX_IACFController
    AAX_Result GetEffectID(AAX_IString* id) const override
    {
        id->Set(effectId.c_str());
        return AAX_SUCCESS;
    }

    AAX_Result GetSampleRate(AAX_CSampleRate* rate) const override
    {
        *rate = static_cast<AAX_CSampleRate>(sampleRateToReport);
        return AAX_SUCCESS;
    }

    AAX_Result GetInputStemFormat(AAX_EStemFormat* format) const override
    {
        *format = input;
        return AAX_SUCCESS;
    }

    AAX_Result GetOutputStemFormat(AAX_EStemFormat* format) const override
    {
        *format = output;
        return AAX_SUCCESS;
    }

    AAX_Result GetSignalLatency(int32_t* samples) const override
    {
        *samples = latency;
        return AAX_SUCCESS;
    }

    AAX_Result GetCycleCount(AAX_EProperty, AAX_CPropertyValue*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetTODLocation(AAX_CTimeOfDay*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result SetSignalLatency(int32_t samples) override
    {
        latency = samples;
        ++latencyCalls;
        return AAX_SUCCESS;
    }

    AAX_Result SetCycleCount(AAX_EProperty*, AAX_CPropertyValue*, int32_t) override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result PostPacket(AAX_CFieldIndex, const void*, uint32_t) override
    {
        return AAX_SUCCESS;
    }

    AAX_Result GetCurrentMeterValue(AAX_CTypeID, float*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetMeterPeakValue(AAX_CTypeID, float*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result ClearMeterPeakValue(AAX_CTypeID) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetMeterClipped(AAX_CTypeID, AAX_CBoolean*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result ClearMeterClipped(AAX_CTypeID) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetMeterCount(uint32_t* count) const override
    {
        *count = 0;
        return AAX_SUCCESS;
    }

    AAX_Result GetNextMIDIPacket(AAX_CFieldIndex*, AAX_CMidiPacket*) override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    // AAX_IACFController_V2
    AAX_Result SendNotification(AAX_CTypeID, const void*, uint32_t) override
    {
        return AAX_SUCCESS;
    }

    AAX_Result GetHybridSignalLatency(int32_t*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetCurrentAutomationTimestamp(AAX_CTransportCounter*) const override
    {
        return AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result GetHostName(AAX_IString* name) const override
    {
        name->Set("MakeASound Tests");
        return AAX_SUCCESS;
    }

    // AAX_IACFAutomationDelegate
    AAX_Result RegisterParameter(AAX_CParamID) override
    {
        ++registered;
        return AAX_SUCCESS;
    }

    AAX_Result UnregisterParameter(AAX_CParamID) override
    {
        --registered;
        return AAX_SUCCESS;
    }

    AAX_Result PostSetValueRequest(AAX_CParamID id, double value) const override
    {
        record(EditKind::SetValue, id, value);

        if (echo && adapter != nullptr)
            adapter->UpdateParameterNormalizedValue(
                id, value, AAX_eUpdateSource_Unspecified);

        return AAX_SUCCESS;
    }

    AAX_Result PostCurrentValue(AAX_CParamID, double) const override
    {
        ++currentValues;
        return AAX_SUCCESS;
    }

    AAX_Result PostTouchRequest(AAX_CParamID id) override
    {
        record(EditKind::Touch, id, 0.0);
        return AAX_SUCCESS;
    }

    AAX_Result PostReleaseRequest(AAX_CParamID id) override
    {
        record(EditKind::Release, id, 0.0);
        return AAX_SUCCESS;
    }

    AAX_Result GetTouchState(AAX_CParamID, AAX_CBoolean* touched) override
    {
        *touched = false;
        return AAX_SUCCESS;
    }

    void record(EditKind kind, AAX_CParamID id, double value) const
    {
        if (edits.size() == edits.capacity())
            return;

        auto edit = Edit {kind, {}, value};
        std::strncpy(edit.id.data(), id, edit.id.size() - 1);
        edits.push_back(edit);
    }

    std::string effectId;
    double sampleRateToReport = sampleRate;
    AAX_EStemFormat input = AAX_eStemFormat_Stereo;
    AAX_EStemFormat output = AAX_eStemFormat_Stereo;
    int32_t latency = -1;
    int latencyCalls = 0;
    int registered = 0;
    mutable int currentValues = 0;
    bool echo = true;
    Format::Adapter* adapter = nullptr;
    mutable std::vector<Edit> edits;
};

// ---------------------------------------------------------------- the fixture

// The effect id of a plugin of the test module, under MakeASound's scheme.
inline std::string effectIdFor(FourCC code)
{
    return std::string(bundleId) + "." + std::string(code.chars, 4);
}

// A port's bundle id, which a JUCE-scheme plugin's effect id defaults to.
constexpr auto juceBundleId = "com.juce.tests";

template <typename P>
OwningPointer<Plugin> createRecorded()
{
    auto plugin = OwningPointer<Plugin>(EA::makeOwned<P>());
    TestPlugins::Module::lastCreated = plugin.get();
    return plugin;
}

// A module of JUCE ports under juceBundleId: the fixture plugin under JUCE's ids
// with the default effect id (the bundle id), and the gain effect, the
// MIDI-echoing instrument and the fixture again under the legacy index ids,
// each with an effect id of its own.
inline const ModuleDescription& juceModule()
{
    static const auto module = []
    {
        auto described = ModuleDescription {};
        described.vendor = "JUCE Tests";
        described.manufacturerCode = "Juce";
        described.plugins.add({.name = "Fixture",
                               .pluginCode = "Fixt",
                               .create = createRecorded<JuceFixtures::FixturePlugin>,
                               .idScheme = IdScheme::JUCE});
        described.plugins.add({.name = "Gain",
                               .pluginCode = "Gain",
                               .create = createRecorded<TestPlugins::GainPlugin>,
                               .idScheme = IdScheme::JUCE,
                               .aaxEffectId = "com.juce.tests.gain"});
        described.plugins.add({.name = "Echo",
                               .category = Category::Instrument,
                               .pluginCode = "Echo",
                               .create = createRecorded<TestPlugins::SynthPlugin>,
                               .idScheme = IdScheme::JUCE,
                               .aaxEffectId = "com.juce.tests.echo"});
        described.plugins.add({.name = "Legacy",
                               .pluginCode = "Lgcy",
                               .create = createRecorded<JuceFixtures::FixturePlugin>,
                               .idScheme = IdScheme::JUCELegacyIndex,
                               .aaxEffectId = "com.juce.tests.legacy"});
        return described;
    }();

    return module;
}

// An effect id of juceModule()'s, for AdapterHost.
struct JuceEffect
{
    std::string id = juceBundleId;
};

// One adapter of the test module, initialized for the stem pair at 48 kHz, with
// host channel memory, every node and the private data the host would fill.
template <typename P>
struct AdapterHost
{
    explicit AdapterHost(FourCC code,
                         AAX_EStemFormat inputFormat = AAX_eStemFormat_Stereo,
                         AAX_EStemFormat outputFormat = AAX_eStemFormat_Stereo)
        : AdapterHost(nullptr, {}, effectIdFor(code), inputFormat, outputFormat)
    {
    }

    // The plugin of juceModule() whose effect id the controller names.
    explicit AdapterHost(JuceEffect effect,
                         AAX_EStemFormat inputFormat = AAX_eStemFormat_Stereo,
                         AAX_EStemFormat outputFormat = AAX_eStemFormat_Stereo)
        : AdapterHost(&juceModule(),
                      juceBundleId,
                      std::move(effect.id),
                      inputFormat,
                      outputFormat)
    {
    }

    // Over `module` under `moduleBundle`, or as the host creates one (the test
    // module under bundleId) when it is null.
    AdapterHost(const ModuleDescription* module,
                std::string_view moduleBundle,
                std::string effectId,
                AAX_EStemFormat inputFormat,
                AAX_EStemFormat outputFormat)
    {
        controller.effectId = std::move(effectId);
        controller.input = inputFormat;
        controller.output = outputFormat;

        if (module != nullptr)
            adapter = std::make_unique<Format::Adapter>(*module, moduleBundle);
        else
            adapter.reset(static_cast<Format::Adapter*>(Format::Adapter::create()));

        controller.adapter = adapter.get();

        TestPlugins::Module::lastCreated = nullptr;
        initResult = adapter->Initialize(controller.unknown());
        pluginPtr = static_cast<P*>(TestPlugins::Module::lastCreated);
        nano::check(initResult == AAX_SUCCESS && pluginPtr != nullptr);

        adapter->ResetFieldData(
            AAX_FIELD_INDEX(Format::AlgorithmContext, privateData),
            &privateData,
            sizeof(privateData));

        inputs.assign(static_cast<size_t>(Format::channelCount(inputFormat)),
                      std::vector<float>(bufferCapacity));
        outputs.assign(static_cast<size_t>(Format::channelCount(outputFormat)),
                       std::vector<float>(bufferCapacity));

        for (auto& channel: inputs)
            inputPointers.push_back(channel.data());

        for (auto& channel: outputs)
            outputPointers.push_back(channel.data());

        transportNode.transport = &transport;

        context.inputs = inputPointers.empty() ? nullptr : inputPointers.data();
        context.outputs = outputPointers.data();
        context.numSamples = &numSamples;
        context.clock = &clock;
        context.midiIn = &midiIn;
        context.midiOut = &midiOut;
        context.transport = &transportNode;
        context.privateData = &privateData;
    }

    ~AdapterHost()
    {
        controller.adapter = nullptr;
        adapter->Uninitialize();
    }

    AdapterHost(const AdapterHost&) = delete;
    AdapterHost& operator=(const AdapterHost&) = delete;

    P& plugin() { return *pluginPtr; }
    PluginWrapper& wrapper() { return *adapter->wrapper(); }

    // The plugin's outputs read and write the inputs' memory.
    void processInPlace()
    {
        for (size_t ch = 0; ch < outputPointers.size() && ch < inputPointers.size();
             ++ch)
            outputPointers[ch] = inputPointers[ch];
    }

    void fillInputs(float value)
    {
        for (auto& channel: inputs)
            std::fill(channel.begin(), channel.end(), value);
    }

    void fillOutputs(float value)
    {
        for (auto& channel: outputs)
            std::fill(channel.begin(), channel.end(), value);
    }

    // The process procedure over this one instance.
    void render(int frames)
    {
        numSamples = frames;
        Format::AlgorithmContext* instances[] = {&context};
        Format::processProc(instances, instances + 1);
        clock += frames;
    }

    // What the plugin's output channel holds, through whichever memory it wrote.
    float output(int channel, int sample) const
    {
        return outputPointers[static_cast<size_t>(channel)][sample];
    }

    // The id the parameter is registered under.
    std::string idOf(const Parameter& param)
    {
        const auto& list = plugin().parameters();
        return std::string(
            Format::registeredId(list.entry(list.indexOf(&param)).aaxId));
    }

    AAX_Result hostWrites(const Parameter& param, double normalized)
    {
        return adapter->UpdateParameterNormalizedValue(
            idOf(param).data(), normalized, AAX_eUpdateSource_Unspecified);
    }

    double hostValue(const Parameter& param)
    {
        auto value = -1.0;
        adapter->GetParameterNormalizedValue(idOf(param).data(), &value);
        return value;
    }

    FakeController controller;
    std::unique_ptr<Format::Adapter> adapter;
    AAX_Result initResult = AAX_ERROR_UNIMPLEMENTED;
    P* pluginPtr = nullptr;

    std::vector<std::vector<float>> inputs;
    std::vector<std::vector<float>> outputs;
    std::vector<float*> inputPointers;
    std::vector<float*> outputPointers;

    int32_t numSamples = 0;
    AAX_CTimestamp clock = 0;
    FakeMidiNode midiIn;
    FakeMidiNode midiOut;
    FakeTransport transport;
    FakeMidiNode transportNode;
    Format::PrivateData privateData {nullptr};
    Format::AlgorithmContext context {};
};

} // namespace AAXHost
