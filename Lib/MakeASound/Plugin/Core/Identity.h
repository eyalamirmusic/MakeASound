#pragma once

#include "Description.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// The ids a host stores for a plugin, by its description's IdScheme and
// overrides, so every format adapter reads them and computes none. A
// parameter's ids are on its ParameterList::Entry, under the identity
// parameterIdentityFor() gives and PluginWrapper applies.
namespace MakeASound
{

// The description's scheme with JUCE's Studio One mask.
ParameterIdentity parameterIdentityFor(const PluginDescription& plugin) noexcept;

// MakeASound's VST3 class id: FUID(manufacturer, plugin, 'VST3', 0), Plug's
// derivation, never to change once shipped.
Vst3ClassId makeASoundVst3ClassId(FourCC manufacturer, FourCC plugin) noexcept;

// The VST3 component class id: the description's override; under a JUCE scheme
// JUCE's VST2-compatible id when juce.vst3CanReplaceVst2 is on and its own
// otherwise; MakeASound's.
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

// AAX_eProperty_PlugInID_Native: the description's aaxNativeId hook; under a
// JUCE scheme JUCE's 'jcaa' derivation from the stems' channel sets;
// otherwise the plugin code plus the stem pair's index (MakeASound's own).
uint32_t aaxNativeIdFor(const PluginDescription& plugin, const AAXStems& stems);

// The effect id AddEffect takes: the description's aaxEffectId; under a JUCE
// scheme the bundle id; otherwise "<bundle id>.<plugin code>".
std::string aaxEffectIdFor(std::string_view bundleId,
                           const PluginDescription& plugin);

} // namespace MakeASound
