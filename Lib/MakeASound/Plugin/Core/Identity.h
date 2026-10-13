#pragma once

#include "Description.h"
#include "IdentityPolicy.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// The ids a host stores for a plugin, by its description's resolved
// IdentityPolicy and overrides, so every format adapter reads them and computes
// none. A parameter's ids are on its ParameterList::Entry, under the policy's
// parameter half, which PluginWrapper applies.
namespace MakeASound
{

// identityPolicyFor(plugin).parameters.
ParameterIdentity parameterIdentityFor(const PluginDescription& plugin) noexcept;

// MakeASound's VST3 class id: FUID(manufacturer, plugin, 'VST3', 0), Plug's
// derivation, never to change once shipped.
Vst3ClassId makeASoundVst3ClassId(FourCC manufacturer, FourCC plugin) noexcept;

// The VST3 component class id: the description's override, else by the
// policy's Vst3ClassRule: JUCE's VST2-compatible id (a JUCE profile with
// juce.vst3CanReplaceVst2 on), JUCE's own (off), or MakeASound's (Native and
// TamberPlug, whose class id Plug's builds already shared).
Vst3ClassId vst3ClassIdFor(const ModuleDescription& module,
                           const PluginDescription& plugin) noexcept;

// How a TUID's 16 bytes hold the four words: Plain on macOS and Linux, COM on
// Windows (the VST3 SDK's COM_COMPATIBLE: the first word little-endian, the
// second's two halves each little-endian). For a moduleinfo.json or a test; an
// adapter passes the words to FUID and the SDK lays them out.
enum class TuidLayout
{
    Plain,
    COM
};

std::array<uint8_t, 16> vst3ClassIdBytes(const Vst3ClassId& id,
                                         TuidLayout layout) noexcept;

// AAX_eProperty_PlugInID_Native: the description's aaxNativeId hook; under
// AAXPluginRule::Juce JUCE's 'jcaa' derivation from the stems' channel sets;
// otherwise the plugin code plus the stem pair's index (MakeASound's own).
uint32_t aaxNativeIdFor(const PluginDescription& plugin, const AAXStems& stems);

// The effect id AddEffect takes: the description's aaxEffectId; under
// AAXPluginRule::Juce the bundle id; otherwise "<bundle id>.<plugin code>".
std::string aaxEffectIdFor(std::string_view bundleId,
                           const PluginDescription& plugin);

} // namespace MakeASound
