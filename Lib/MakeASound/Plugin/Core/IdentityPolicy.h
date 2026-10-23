#pragma once

#include "Description.h"

namespace MakeASound
{

// The VST3 component class id.
enum class Vst3ClassRule
{
    // FUID(manufacturer, plugin, 'VST3', 0) (makeASoundVst3ClassId).
    Native,

    // JUCE's own (Compat::juceVst3ClassId).
    JuceVst3,

    // JUCE's VST2-compatible one, under JUCE_VST3_CAN_REPLACE_VST2
    // (Compat::juceVst2ClassId).
    JuceVst2
};

// AAX_eProperty_PlugInID_Native and the effect id.
enum class AAXPluginRule
{
    // The plugin code plus the stem pair's index, and "<bundle id>.<code>".
    Native,

    // JUCE's 'jcaa' derivation from the stems' channel sets, and the bundle id.
    Juce
};

// Every rule by which a host addresses a plugin and its parameters, resolved
// from its description's IdScheme profile (policyFor) and overrides
// (identityPolicyFor). Format adapters read only this, never the profile.
struct IdentityPolicy
{
    // What the ParameterList computes and the AU and AUv3 adapters publish.
    ParameterIdentity parameters;

    Vst3ClassRule vst3Class = Vst3ClassRule::Native;
    AAXPluginRule aaxPlugin = AAXPluginRule::Native;

    // Not an id, but a JUCE build's all the same: whether the standalone, with
    // no settings of its own, imports a JUCE build's settings file
    // (StandaloneHost::juceImportPath).
    bool juceStandaloneImport = false;

    constexpr bool operator==(const IdentityPolicy&) const = default;
};

// The profile's rules, with JUCE's settings where it is one of JUCE's.
constexpr IdentityPolicy policyFor(IdScheme scheme,
                                   const JuceSettings& juce) noexcept
{
    auto policy = IdentityPolicy {};
    auto& params = policy.parameters;

    if (scheme == IdScheme::TamberPlug)
    {
        params.paramIds = ParamIdRule::Index;
        params.addedBypassInAU = BypassPlacement::Unlisted;
        params.auOrder = AUOrder::IndexThenHintId;
        params.auValues = AUValueSpace::Plain;
        params.auUnits = AUUnits::Plug;
        params.hostGroups = false;
        params.foreignPlugStateIsNative = true;
        return policy;
    }

    if (!isJuceScheme(scheme))
        return policy;

    auto legacy = scheme == IdScheme::JUCELegacyIndex;

    params.paramIds = legacy ? ParamIdRule::Index : ParamIdRule::JuceKeyHash;
    params.juceMask31 = juce.studioOneCompatibleParameters;
    params.registration = Registration::All;
    params.juceChecks = true;
    params.addedBypassFor = AddedBypassFor::AllPlugins;
    params.addedBypassId = legacy ? AddedBypassId::Index : AddedBypassId::JuceByps;
    params.addedBypassInAU = BypassPlacement::Unlisted;
    params.auv3 = legacy ? AUv3Ids::IndexAndKey : AUv3Ids::JuceHash64AndKey;
    params.aaxIds = legacy ? AAXIdRule::DecimalIndex : AAXIdRule::KeyOrOverride;
    params.groups = GroupIdRule::OwnId;
    params.auOrder = legacy ? AUOrder::SdkIndexed : AUOrder::HintThenId;
    params.auValues = AUValueSpace::JuceScaled;
    params.auUnits = AUUnits::Juce;
    params.juceLegacyAutomationType = juce.forceLegacyParameterAutomationType;
    params.vst3Controllers.layout = Vst3ControllerLayout::JuceContiguous;
    params.vst3Controllers.base = juceMidiControllerBase;

    policy.vst3Class =
        juce.vst3CanReplaceVst2 ? Vst3ClassRule::JuceVst2 : Vst3ClassRule::JuceVst3;
    policy.aaxPlugin = AAXPluginRule::Juce;
    policy.juceStandaloneImport = true;
    return policy;
}

// The description's profile and settings resolved, then its overrides applied.
IdentityPolicy identityPolicyFor(const PluginDescription& plugin) noexcept;

} // namespace MakeASound
