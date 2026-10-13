#pragma once

#include "../../Common/Common.h"
#include "../Parameters/ParameterIdentity.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace MakeASound
{

class Plugin;

// Four characters, the identity AU uses natively. VST3's 128-bit UID is derived
// from the module's manufacturerCode plus the plugin's pluginCode, so a stable
// four-character code is all anyone writes: `.pluginCode = "Gain"`.
struct FourCC
{
    constexpr FourCC() = default;

    constexpr FourCC(const char (&s)[5])
        : chars {s[0], s[1], s[2], s[3]}
    {
    }

    constexpr uint32_t toUint32() const noexcept
    {
        return (uint32_t(static_cast<unsigned char>(chars[0])) << 24)
               | (uint32_t(static_cast<unsigned char>(chars[1])) << 16)
               | (uint32_t(static_cast<unsigned char>(chars[2])) << 8)
               | uint32_t(static_cast<unsigned char>(chars[3]));
    }

    constexpr bool operator==(const FourCC&) const = default;

    char chars[4] {};
};

enum class Category
{
    Effect,
    Instrument,

    // Declares MIDI in and out plus at least one audio output bus: hosts drive
    // MIDI processing through the render call. AU maps this to 'aumi'; VST3 has
    // no such class, so it ships as an instrument.
    MidiEffect
};

// The VST3 subcategory string a category implies when the description names
// none: Effect -> "Fx", Instrument -> "Instrument|Synth".
std::string_view defaultSubcategory(Category category) noexcept;

// Pro Tools' plug-in categories, SDK-free. Default is Instrument for
// Category::Instrument and MidiEffect, Effect otherwise.
enum class AAXCategory
{
    Default,
    EQ,
    Dynamics,
    PitchShift,
    Reverb,
    Delay,
    Modulation,
    Harmonic,
    NoiseReduction,
    Dither,
    SoundField,
    Effect,
    Instrument
};

using PluginCreateFn = std::function<OwningPointer<Plugin>()>;

// A VST3 class id as the four 32-bit words Steinberg's FUID(l1, l2, l3, l4)
// takes. The words are the same on every platform; only the 16 bytes a TUID
// holds differ, which vst3ClassIdBytes() (Core/Identity.h) lays out.
using Vst3ClassId = std::array<uint32_t, 4>;

// One AAX component's main stems, for the native-id hook: channel counts, 0 for
// no bus, and the stem pair's index in the AAX adapter's append-only list.
struct AAXStems
{
    int inputChannels = 0;
    int outputChannels = 0;
    int index = 0;
};

using AAXNativeIdFn = std::function<uint32_t(const AAXStems& stems)>;

// The JUCE settings that name a ported plugin and its parameters, with JUCE
// 8.0.12's defaults. Read only when the description's idScheme is a JUCE one.
// JUCE_FORCE_USE_LEGACY_PARAM_IDS is not here: it is IdScheme::JUCELegacyIndex.
struct JuceSettings
{
    // JUCE_USE_STUDIO_ONE_COMPATIBLE_PARAMETERS (1): VST3 and AU parameter ids
    // masked to 31 bits.
    bool studioOneCompatibleParameters = true;

    // JUCE_FORCE_LEGACY_PARAMETER_AUTOMATION_TYPE (0): every AU parameter 0..1,
    // none Indexed or Boolean, none IsHighResolution.
    bool forceLegacyParameterAutomationType = false;

    // JUCE_VST3_CAN_REPLACE_VST2 (1 in juce_audio_plugin_client.h, though JUCE's
    // CMake example sets 0): the VST3 class id is the VST2 one, 'VST' plus the
    // VST2 id plus the name (Compat::juceVst2ClassId), not JUCE's own
    // (Compat::juceVst3ClassId). Ask the porting author which they shipped.
    bool vst3CanReplaceVst2 = true;

    // JucePlugin_Name, which the VST2 class id takes up to nine characters of;
    // the description's name when empty.
    std::string_view pluginName = {};

    // JucePlugin_VSTUniqueID; the plugin code when unset, as JUCE's CMake
    // defaults it.
    std::optional<FourCC> vstUniqueId = {};

    constexpr bool operator==(const JuceSettings&) const = default;
};

// Per-concern overrides of the rules a description's IdScheme resolves to
// (IdentityPolicy, Core/IdentityPolicy.h), each winning over the profile where
// set. There is one only where a port needs it.
struct IdentityOverrides
{
    // ParameterIdentity::indexedThroughVersion, under an index profile
    // (JUCELegacyIndex): set it to the hint the port shipped with, 1 for most,
    // the first release that adds a parameter other than at the end.
    std::optional<int> indexedThroughVersion = {};

    // The AU value space, units and flags, and whether an AU lists the bypass
    // PluginWrapper adds (ParameterIdentity's auValues, auUnits and
    // addedBypassInAU): e.g. MakeASound's display flags over Plug's ids.
    std::optional<AUValueSpace> auValues = {};
    std::optional<AUUnits> auUnits = {};
    std::optional<BypassPlacement> addedBypassInAU = {};

    constexpr bool operator==(const IdentityOverrides&) const = default;
};

// One plugin class within a module.
struct PluginDescription
{
    std::string_view name;
    std::string_view version = "1.0.0";
    Category category = Category::Effect;
    std::string_view subcategory = {};
    AAXCategory aaxCategory = AAXCategory::Default;

    // Unique per plugin within a manufacturer.
    FourCC pluginCode;

    PluginCreateFn create;

    // Identity. Every id a host stores for this plugin and its parameters comes
    // from here: MakeASound's by default, a JUCE build's for a port that must
    // keep its users' sessions (Core/Identity.h computes them). Per plugin
    // rather than per module, since JUCE's settings are per binary and a module
    // may hold a port beside native plugins; on the description rather than the
    // Plugin, since a VST3 factory names its classes before any instance exists
    // and a port should not have to change its processor to keep its ids.
    IdScheme idScheme = IdScheme::Native;
    JuceSettings juce = {};
    IdentityOverrides overrides = {};

    // Overrides, each winning over the scheme. The VST3 component class id.
    std::optional<Vst3ClassId> vst3ClassId = {};

    // The AU componentType ('aufx', 'aumu', 'aumf', 'aumi'); derived from the
    // category and layout when unset.
    std::optional<FourCC> auType = {};

    // Whether the VST3 build receives MIDI CC, channel aftertouch, pitch bend
    // and program change. VST3 hosts deliver those only through IMidiMapping,
    // onto hidden parameters the adapter registers: 16 channels x 131
    // controllers per MIDI input bus. Unset follows the category (on for
    // Category::Instrument, off otherwise; vst3MidiControllersFor()). Off, no
    // such parameter exists and the mapping answers kResultFalse: notes, poly
    // pressure, SysEx and any controller a host sends as an event still arrive,
    // but a host's mapped CC, bend, aftertouch and program change never reach
    // the plugin. AU, AUv3 and AAX deliver every MIDI message natively and
    // ignore this.
    std::optional<bool> vst3MidiControllers = {};

    // The id AAX's AddEffect takes: "<bundle id>.<plugin code>" by default,
    // the bundle id under a JUCE scheme (JUCE's JucePlugin_AAXIdentifier
    // defaults to it).
    std::string_view aaxEffectId = {};

    // AAX_eProperty_PlugInID_Native per component, the hook JUCE calls
    // AAXClientExtensions::getPluginIDForMainBusConfig: by default the plugin
    // code plus the stem pair's index, JUCE's 'jcaa' derivation under a JUCE
    // scheme (aaxNativeIdFor()).
    AAXNativeIdFn aaxNativeId = {};
};

// The description's vst3MidiControllers, or by category when unset: on for an
// instrument, off for an effect or a MIDI effect.
bool vst3MidiControllersFor(const PluginDescription& plugin) noexcept;

// A module may host more than one plugin class: VST3 factories and AU bundles
// both allow it.
struct ModuleDescription
{
    std::string_view vendor;
    std::string_view url = {};
    std::string_view email = {};

    // The bundle's version, distinct from each plugin's own. Bump it whenever
    // `plugins` changes: macOS caches a bundle's AudioComponents list keyed by
    // CFBundleVersion and will not re-read one whose version has not moved.
    std::string_view version = "1.0.0";

    // Unique per vendor. Used natively by AU and mixed into every VST3 UID.
    FourCC manufacturerCode;

    Vector<PluginDescription> plugins;
};

// Implemented exactly once per plugin module, in the plugin's own TU.
ModuleDescription describeModule();

} // namespace MakeASound
