#pragma once

#include "AAXCommon.h"
#include "../Core/Description.h"
#include "../../Audio/BusLayout.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace MakeASound::AAX
{

// One component's main-bus pair.
struct StemPair
{
    AAX_EStemFormat input = AAX_eStemFormat_None;
    AAX_EStemFormat output = AAX_eStemFormat_None;
};

// Append-only: a component's native id is the plugin's code plus its index here,
// so an id once shipped never changes.
std::span<const StemPair> stemPairs() noexcept;

uint32_t nativePluginId(const PluginDescription& plugin, int stemIndex) noexcept;

// The AAX_EPlugInCategory bits for the description's aaxCategory.
uint32_t categoryFor(const PluginDescription& plugin) noexcept;

// The declared layout with its main buses at the pair's channel counts, or none
// when the pair does not fit its shape: an input stem without an input bus, or
// none with one.
std::optional<BusLayout> layoutFor(const BusLayout& declared, StemPair pair);

int channelCount(AAX_EStemFormat format) noexcept;

// "<bundle id>.<plugin code>", the id AddEffect takes.
std::string effectIdFor(std::string_view bundleId, const PluginDescription& plugin);

// Null when no plugin of the module has that effect id's code.
const PluginDescription* findPlugin(const ModuleDescription& module,
                                    std::string_view effectId) noexcept;

// One effect per plugin, one component per stem pair its plugin accepts.
// Constructs each plugin once to read its layout and bypass.
AAX_Result describeEffects(AAX_ICollection& collection,
                           const ModuleDescription& module,
                           std::string_view bundleId,
                           std::string_view packageName);

} // namespace MakeASound::AAX
