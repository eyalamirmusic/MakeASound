#pragma once

#include <cstdint>
#include <string_view>

namespace MakeASound
{

// How hosts address a plugin's parameters, and the plugin itself. MakeASound's
// own scheme is the default; the two JUCE ones are for a plugin ported from
// JUCE that must keep its users' sessions and automation, and follow its
// JUCE_FORCE_USE_LEGACY_PARAM_IDS setting (0, JUCE's default: JUCE).
enum class IdScheme
{
    // VST3 and AU ids FNV-1a of the id path, an AUv3 address the same and its
    // identifier the id path, an AAX id the host id in eight hex digits.
    MakeASound,

    // JUCE's ids from each parameter's hostKey (its leaf id): VST3 and AU
    // String::hashCode, masked to 31 bits unless juceStudioOneMask is off, an
    // AUv3 address hashCode64 and its identifier the key, an AAX id the key.
    JUCE,

    // JUCE_FORCE_USE_LEGACY_PARAM_IDS: every format addresses a parameter by its
    // index among all of them, the AAX id that index in decimal.
    JUCELegacyIndex
};

constexpr bool isJuceScheme(IdScheme scheme) noexcept
{
    return scheme != IdScheme::MakeASound;
}

// The half of a plugin's identity its ParameterList computes. PluginWrapper
// applies it, from the plugin's PluginDescription (parameterIdentityFor()).
struct ParameterIdentity
{
    IdScheme scheme = IdScheme::MakeASound;

    // JUCE_USE_STUDIO_ONE_COMPATIBLE_PARAMETERS (1, JUCE's default): JUCE masks
    // its VST3 and AU ids to 31 bits. Off, a host id may be 2^31 or more. Read
    // only under IdScheme::JUCE.
    bool juceStudioOneMask = true;

    constexpr bool operator==(const ParameterIdentity&) const = default;
};

// The ids JUCE's wrappers give the parameters they make themselves.

// The VST3 bypass JUCE adds for a plugin with none, 'byps', under IdScheme::JUCE
// (under JUCELegacyIndex it is the next index, as for any other parameter).
constexpr uint32_t juceVst3BypassId = 0x62797073u;

// The VST3 program parameter JUCE adds for a plugin with more than one program,
// 'prst'. Reserved for factory presets (WP10.12): nothing makes it yet.
constexpr uint32_t juceVst3ProgramId = 0x70727374u;

// AAX's master bypass, the id JUCE gives the bypass parameter whether the plugin
// declared it or the wrapper added it, in either JUCE scheme.
constexpr std::string_view juceAAXBypassId = "MasterBypassID";

// The longest AAX_CParamID JUCE lets through without an assert.
constexpr int juceAAXMaxIdLength = 31;

} // namespace MakeASound
