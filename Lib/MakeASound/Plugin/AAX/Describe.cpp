#include "Describe.h"
#include "Adapter.h"
#include "AlgorithmContext.h"
#include "View.h"
#include "../Core/Identity.h"
#include "../Core/Plugin.h"

#include <AAX_Exception.h>

#include <algorithm>
#include <charconv>

namespace MakeASound::AAX
{

namespace
{

constexpr StemPair allStemPairs[] = {
    {AAX_eStemFormat_Mono, AAX_eStemFormat_Mono},
    {AAX_eStemFormat_Mono, AAX_eStemFormat_Stereo},
    {AAX_eStemFormat_Stereo, AAX_eStemFormat_Stereo},
    {AAX_eStemFormat_None, AAX_eStemFormat_Mono},
    {AAX_eStemFormat_None, AAX_eStemFormat_Stereo},
};

// "major.minor.patch" as major << 16 | minor << 8 | patch, a bad part 0.
uint32_t packageVersion(std::string_view version) noexcept
{
    auto result = uint32_t {0};

    for (auto shift: {16, 8, 0})
    {
        auto dot = version.find('.');
        auto part = version.substr(0, dot);
        auto value = 0u;
        std::from_chars(part.data(), part.data() + part.size(), value);
        result |= std::min(value, 255u) << shift;

        if (dot == std::string_view::npos)
            break;

        version.remove_prefix(dot + 1);
    }

    return result;
}

// Pro Tools picks the longest name that fits where it shows one.
void addNames(AAX_IEffectDescriptor& effect, std::string_view name)
{
    auto checked = AAX_CheckedResult {};
    auto full = std::string(name);
    checked = effect.AddName(full.c_str());

    for (auto length: {31, 15, 7, 4})
    {
        if (static_cast<int>(name.size()) <= length)
            continue;

        auto shorter = std::string(name.substr(0, size_t(length)));
        checked = effect.AddName(shorter.c_str());
    }
}

void describeComponent(AAX_IEffectDescriptor& effect,
                       const ModuleDescription& module,
                       const PluginDescription& plugin,
                       const BusLayout& layout,
                       int stemIndex,
                       bool canBypass)
{
    auto checked = AAX_CheckedResult {};
    auto* component = effect.NewComponentDescriptor();

    if (component == nullptr)
        checked = AAX_ERROR_NULL_OBJECT;

    auto pair = allStemPairs[stemIndex];
    auto name = std::string(plugin.name);

    checked = component->AddAudioIn(AAX_FIELD_INDEX(AlgorithmContext, inputs));
    checked = component->AddAudioOut(AAX_FIELD_INDEX(AlgorithmContext, outputs));
    checked = component->AddAudioBufferLength(
        AAX_FIELD_INDEX(AlgorithmContext, numSamples));
    checked = component->AddClock(AAX_FIELD_INDEX(AlgorithmContext, clock));

    // A field with no node still gets a port, so the context has no gaps.
    auto midiNode = [&](AAX_CFieldIndex field, AAX_EMIDINodeType type, bool wanted)
    {
        if (wanted)
            checked = component->AddMIDINode(field, type, name.c_str(), 0xffff);
        else
            checked = component->AddPrivateData(
                field, sizeof(float), AAX_ePrivateDataOptions_DefaultOptions);
    };

    midiNode(AAX_FIELD_INDEX(AlgorithmContext, midiIn),
             AAX_eMIDINodeType_LocalInput,
             !layout.midiInputs.empty());
    midiNode(AAX_FIELD_INDEX(AlgorithmContext, midiOut),
             AAX_eMIDINodeType_LocalOutput,
             !layout.midiOutputs.empty());

    checked = component->AddMIDINode(AAX_FIELD_INDEX(AlgorithmContext, transport),
                                     AAX_eMIDINodeType_Transport,
                                     "Transport",
                                     0xffff);

    checked =
        component->AddPrivateData(AAX_FIELD_INDEX(AlgorithmContext, privateData),
                                  sizeof(PrivateData),
                                  AAX_ePrivateDataOptions_DefaultOptions);

    auto* properties = component->NewPropertyMap();

    if (properties == nullptr)
        checked = AAX_ERROR_NULL_OBJECT;

    auto property = [&](AAX_EProperty id, AAX_CPropertyValue value)
    { checked = properties->AddProperty(id, value); };

    property(AAX_eProperty_ManufacturerID,
             AAX_CPropertyValue(module.manufacturerCode.toUint32()));
    property(AAX_eProperty_ProductID,
             AAX_CPropertyValue(plugin.pluginCode.toUint32()));
    property(AAX_eProperty_PlugInID_Native,
             AAX_CPropertyValue(nativePluginId(plugin, stemIndex)));
    property(AAX_eProperty_InputStemFormat, pair.input);
    property(AAX_eProperty_OutputStemFormat, pair.output);
    property(AAX_eProperty_CanBypass, canBypass);
    property(AAX_eProperty_UsesTransport, true);
    property(AAX_eProperty_Constraint_Location,
             AAX_eConstraintLocationMask_DataModel);
    property(AAX_eProperty_Constraint_Topology, AAX_eConstraintTopology_Monolithic);
    property(AAX_eProperty_Constraint_MultiMonoSupport, false);

    checked = component->AddProcessProc_Native(processProc, properties);
    checked = effect.AddComponent(component);
}

// False when the plugin accepts no stem pair, and then nothing is added.
bool describeEffect(AAX_ICollection& collection,
                    const ModuleDescription& module,
                    const PluginDescription& plugin,
                    std::string_view bundleId)
{
    auto instance = plugin.create();
    auto declared = instance->getBusLayout();

    auto accepted = Vector<std::pair<int, BusLayout>> {};

    for (auto i = 0; i < static_cast<int>(std::size(allStemPairs)); ++i)
        if (auto layout = layoutFor(declared, allStemPairs[i]))
            if (instance->acceptsLayout(*layout))
                accepted.add({i, *layout});

    if (accepted.empty())
        return false;

    auto checked = AAX_CheckedResult {};
    auto* effect = collection.NewDescriptor();

    if (effect == nullptr)
        checked = AAX_ERROR_NULL_OBJECT;

    addNames(*effect, plugin.name);
    checked = effect->AddCategory(categoryFor(plugin));

    // The adapter's wrapper adds a bypass to an effect that has none, and under
    // a JUCE scheme to any plugin that has none.
    auto canBypass =
        instance->parameters().bypassIndex() >= 0
        || PluginWrapper::addsBypass(*instance, parameterIdentityFor(plugin));

    for (auto& [index, layout]: accepted)
        describeComponent(*effect, module, plugin, layout, index, canBypass);

    checked = effect->AddProcPtr(reinterpret_cast<void*>(&Adapter::create),
                                 kAAX_ProcPtrID_Create_EffectParameters);

    if (hasAAXView())
        checked = effect->AddProcPtr(reinterpret_cast<void*>(&createAAXView),
                                     kAAX_ProcPtrID_Create_EffectGUI);

    auto id = aaxEffectIdFor(bundleId, plugin);
    checked = collection.AddEffect(id.c_str(), effect);
    return true;
}

} // namespace

std::span<const StemPair> stemPairs() noexcept
{
    return allStemPairs;
}

uint32_t nativePluginId(const PluginDescription& plugin, int stemIndex)
{
    auto pair = allStemPairs[stemIndex];
    return aaxNativeIdFor(
        plugin,
        AAXStems {channelCount(pair.input), channelCount(pair.output), stemIndex});
}

uint32_t categoryFor(const PluginDescription& plugin) noexcept
{
    switch (plugin.aaxCategory)
    {
        case AAXCategory::EQ:
            return AAX_ePlugInCategory_EQ;
        case AAXCategory::Dynamics:
            return AAX_ePlugInCategory_Dynamics;
        case AAXCategory::PitchShift:
            return AAX_ePlugInCategory_PitchShift;
        case AAXCategory::Reverb:
            return AAX_ePlugInCategory_Reverb;
        case AAXCategory::Delay:
            return AAX_ePlugInCategory_Delay;
        case AAXCategory::Modulation:
            return AAX_ePlugInCategory_Modulation;
        case AAXCategory::Harmonic:
            return AAX_ePlugInCategory_Harmonic;
        case AAXCategory::NoiseReduction:
            return AAX_ePlugInCategory_NoiseReduction;
        case AAXCategory::Dither:
            return AAX_ePlugInCategory_Dither;
        case AAXCategory::SoundField:
            return AAX_ePlugInCategory_SoundField;
        case AAXCategory::Effect:
            return AAX_EPlugInCategory_Effect;
        case AAXCategory::Instrument:
            return AAX_ePlugInCategory_SWGenerators;
        case AAXCategory::Default:
            break;
    }

    return plugin.category == Category::Effect ? AAX_EPlugInCategory_Effect
                                               : AAX_ePlugInCategory_SWGenerators;
}

int channelCount(AAX_EStemFormat format) noexcept
{
    return format == AAX_eStemFormat_None ? 0
                                          : AAX_STEM_FORMAT_CHANNEL_COUNT(format);
}

std::optional<BusLayout> layoutFor(const BusLayout& declared, StemPair pair)
{
    auto hasInputStem = pair.input != AAX_eStemFormat_None;

    if (hasInputStem == declared.inputs.empty() || declared.outputs.empty())
        return std::nullopt;

    auto layout = declared;

    if (hasInputStem)
        layout.inputs[0].numChannels = channelCount(pair.input);

    layout.outputs[0].numChannels = channelCount(pair.output);
    return layout;
}

const PluginDescription* findPlugin(const ModuleDescription& module,
                                    std::string_view bundleId,
                                    std::string_view effectId)
{
    for (auto& plugin: module.plugins)
        if (aaxEffectIdFor(bundleId, plugin) == effectId)
            return &plugin;

    return nullptr;
}

AAX_Result describeEffects(AAX_ICollection& collection,
                           const ModuleDescription& module,
                           std::string_view bundleId,
                           std::string_view packageName)
{
    try
    {
        auto checked = AAX_CheckedResult {};
        auto described = false;

        for (auto& plugin: module.plugins)
            described =
                describeEffect(collection, module, plugin, bundleId) || described;

        if (!described)
            return AAX_ERROR_NULL_OBJECT;

        auto vendor = std::string(module.vendor);
        checked = collection.SetManufacturerName(vendor.c_str());

        auto package = std::string(packageName);
        checked = collection.AddPackageName(package.c_str());
        checked = collection.SetPackageVersion(packageVersion(module.version));
        return AAX_SUCCESS;
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

} // namespace MakeASound::AAX
