#include "Identity.h"
#include "../Compat/JuceIdentity.h"

namespace MakeASound
{

IdentityPolicy identityPolicyFor(const PluginDescription& plugin) noexcept
{
    auto policy = policyFor(plugin.idScheme, plugin.juce);
    const auto& overrides = plugin.overrides;

    auto& params = policy.parameters;

    if (overrides.indexedThroughVersion)
        params.indexedThroughVersion = *overrides.indexedThroughVersion;

    if (overrides.auValues)
        params.auValues = *overrides.auValues;

    if (overrides.auUnits)
        params.auUnits = *overrides.auUnits;

    if (overrides.addedBypassInAU)
        params.addedBypassInAU = *overrides.addedBypassInAU;

    return policy;
}

ParameterIdentity parameterIdentityFor(const PluginDescription& plugin) noexcept
{
    return identityPolicyFor(plugin).parameters;
}

Vst3ClassId makeASoundVst3ClassId(FourCC manufacturer, FourCC plugin) noexcept
{
    return {
        manufacturer.toUint32(), plugin.toUint32(), FourCC("VST3").toUint32(), 0};
}

Vst3ClassId vst3ClassIdFor(const ModuleDescription& module,
                           const PluginDescription& plugin) noexcept
{
    if (plugin.vst3ClassId)
        return *plugin.vst3ClassId;

    auto rule = identityPolicyFor(plugin).vst3Class;

    if (rule == Vst3ClassRule::Native)
        return makeASoundVst3ClassId(module.manufacturerCode, plugin.pluginCode);

    if (rule == Vst3ClassRule::JuceVst3)
        return Compat::juceVst3ClassId(module.manufacturerCode, plugin.pluginCode);

    auto name =
        plugin.juce.pluginName.empty() ? plugin.name : plugin.juce.pluginName;
    auto uniqueId = plugin.juce.vstUniqueId.value_or(plugin.pluginCode);
    return Compat::juceVst2ClassId(uniqueId.toUint32(), name);
}

std::array<uint8_t, 16> vst3ClassIdBytes(const Vst3ClassId& id,
                                         TuidLayout layout) noexcept
{
    auto bytes = std::array<uint8_t, 16> {};

    for (auto word = size_t {0}; word < 4; ++word)
        for (auto byte = size_t {0}; byte < 4; ++byte)
            bytes[word * 4 + byte] =
                static_cast<uint8_t>(id[word] >> (24 - 8 * byte));

    if (layout == TuidLayout::COM)
    {
        auto plain = bytes;
        bytes[0] = plain[3];
        bytes[1] = plain[2];
        bytes[2] = plain[1];
        bytes[3] = plain[0];
        bytes[4] = plain[5];
        bytes[5] = plain[4];
        bytes[6] = plain[7];
        bytes[7] = plain[6];
    }

    return bytes;
}

uint32_t aaxNativeIdFor(const PluginDescription& plugin, const AAXStems& stems)
{
    if (plugin.aaxNativeId)
        return plugin.aaxNativeId(stems);

    if (identityPolicyFor(plugin).aaxPlugin == AAXPluginRule::Juce)
        return Compat::juceAAXPluginId(
            Compat::juceAAXChannelSetIndex(stems.inputChannels),
            Compat::juceAAXChannelSetIndex(stems.outputChannels));

    return plugin.pluginCode.toUint32() + static_cast<uint32_t>(stems.index);
}

std::string aaxEffectIdFor(std::string_view bundleId,
                           const PluginDescription& plugin)
{
    if (!plugin.aaxEffectId.empty())
        return std::string(plugin.aaxEffectId);

    auto id = std::string(bundleId);

    if (identityPolicyFor(plugin).aaxPlugin == AAXPluginRule::Native)
    {
        id += '.';
        id.append(plugin.pluginCode.chars, 4);
    }

    return id;
}

} // namespace MakeASound
