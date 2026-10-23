#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace MakeASound
{

// How hosts address a plugin's parameters, and the plugin itself: a
// compatibility profile, which policyFor() (Core/IdentityPolicy.h) resolves into
// the rules every format adapter reads. MakeASound's own scheme, Native, is the
// default; the two JUCE ones are for a plugin ported from JUCE that must keep its
// users' sessions and automation, and follow its JUCE_FORCE_USE_LEGACY_PARAM_IDS
// setting (0, JUCE's default: JUCE).
enum class IdScheme
{
    // VST3 and AU ids FNV-1a of the id path, an AUv3 address the same and its
    // identifier the id path, an AAX id the host id in eight hex digits.
    Native,

    // Native's earlier name.
    MakeASound [[deprecated("IdScheme::MakeASound is now IdScheme::Native")]] =
        Native,

    // JUCE's ids from each parameter's hostKey (its leaf id): VST3 and AU
    // String::hashCode, masked to 31 bits unless juceStudioOneMask is off, an
    // AUv3 address hashCode64 and its identifier the key, an AAX id the key.
    JUCE,

    // JUCE_FORCE_USE_LEGACY_PARAM_IDS: every format addresses a parameter by its
    // index among all of them, the AAX id that index in decimal.
    JUCELegacyIndex,

    // A plugin ported from Tamber Plug: VST3 and AU ids the index among every
    // leaf, hidden ones included (so sparse where one is hidden), only the
    // exposed ones registered and listed, the AU list by index; the bypass
    // PluginWrapper adds is left to the AU bypass property; AUv3 and AAX ids,
    // the VST3 class id and the AAX plug-in ids MakeASound's (Plug shipped no
    // AUv3 or AAX, and its class id is MakeASound's). A post-port parameter
    // (IdentityOverrides::indexedThroughVersion) takes FNV-1a of its path.
    TamberPlug
};

// Whether the profile is one of JUCE's. Read only by policyFor(): everything
// else reads the resolved rules.
constexpr bool isJuceScheme(IdScheme scheme) noexcept
{
    return scheme == IdScheme::JUCE || scheme == IdScheme::JUCELegacyIndex;
}

// The VST3 and AU id of an entry.
enum class ParamIdRule
{
    // hostIdFor() of the id path.
    PathHash,

    // JUCE's String::hashCode of the hostKey, masked to 31 bits when
    // ParameterIdentity::juceMask31 says so.
    JuceKeyHash,

    // The entry's position in the depth-first list.
    Index
};

// Which entries the VST3 and AAX adapters register, and an AU lists.
enum class Registration
{
    // The host-exposed (automatable) ones only.
    ExposedOnly,

    // Every one, a non-automatable one as not automatable (NonRealTime in an
    // AU), as JUCE's wrappers do.
    All
};

// The VST3 and AU id of the bypass PluginWrapper adds for an effect with none.
enum class AddedBypassId
{
    // hostIdFor("__bypass"), as any other entry.
    PathHash,

    // 'byps', JUCE's (juceVst3BypassId).
    JuceByps,

    // Its index, as any other entry.
    Index
};

// Whether an AU host sees a bypass in the parameter list, or only through
// kAudioUnitProperty_BypassEffect (AUv3: shouldBypassEffect). Set apart for the
// bypass PluginWrapper adds (addedBypassInAU) and the one the plugin declares
// (declaredBypassInAU).
enum class BypassPlacement
{
    // Listed: the added one with versionHint 0, so first in a (hint, id) order;
    // a declared one with its own hint.
    Listed,

    // JUCE's AU and AUv3 do not list the bypass they answer through the
    // property, so listing it would move every Logic automation index by one.
    // That holds for a JUCE plugin's own bypass too (getBypassParameter()),
    // which a port declares with {.bypass = true}.
    Unlisted
};

// Which plugins that declare no bypass get the one PluginWrapper adds.
enum class AddedBypassFor
{
    // One whose declared layout has a main input: an effect, not an instrument.
    Effects,

    // Every one, instruments included, as JUCE's VST3 and AAX wrappers do. With
    // no main input, a bypassed instrument outputs silence.
    AllPlugins
};

// The AUv3 AUParameter address and identifier.
enum class AUv3Ids
{
    // The host id and the id path.
    HostIdAndPath,

    // String::hashCode64 of the hostKey, never masked, and the hostKey.
    JuceHash64AndKey,

    // The index and the hostKey.
    IndexAndKey
};

// The AAX_CParamID.
enum class AAXIdRule
{
    // The host id in eight lowercase hex digits.
    HexHostId,

    // ParameterOptions::aaxId, else the hostKey; "MasterBypassID" for the
    // bypass, declared or added.
    KeyOrOverride,

    // The index in decimal; "MasterBypassID" for the bypass.
    DecimalIndex
};

// A named group's ids (ParameterList::Group): its AUv3 AUParameterGroup
// identifier, its VST3 UnitID and its AU clump name.
enum class GroupIdRule
{
    // The identifier is the group's id path, the unit id FNV-1a of it
    // (hostIdFor), and the clump name the groups' names joined by spaces, as a
    // parameter's display name joins them.
    Path,

    // JUCE's group id, as JUCE's tree names it: the group's own id or, under
    // ParameterIdentity::juceKeyGroupSeparator, its enclosing groups' own ids
    // and its own joined by the separator ("DriveFilter-EQ"). The unit id is
    // String::hashCode of it & 0x7fffffff, as JUCE's VST3 wrapper's; the clump
    // name the groups' names joined by the separator, '|' (JUCE's default)
    // without one, as JUCE's AU's.
    OwnId
};

// GetParameterList's order.
enum class AUOrder
{
    // The listed entries sorted by host id and then, stably, by versionHint, as
    // JUCE's AU does, so a parameter added with a higher hint never moves an
    // older one's index, which Logic automates by.
    HintThenId,

    // The AudioUnitSDK's own indexed list, ids 0..n-1 (JUCE's legacy ids). With
    // post-port entries, the indexed entries by index and then the post-port
    // ones by (hint, id), as a list of the adapter's own.
    SdkIndexed,

    // The indexed entries by index, then the post-port ones by (hint, id), as a
    // list of the adapter's own: Plug's ascending index, sparse ids allowed.
    IndexThenHintId
};

// The range and value space an AU host sees.
enum class AUValueSpace
{
    // Plain, or 0..1 where the curve is Other (AU::publishesNormalized).
    NativeCurve,

    // JUCE's: 0..1, or 0..steps for a choice and a bool, every value the
    // normalized one times the maximum (Compat::juceAUParameterInfo).
    JuceScaled,

    // Plain min..max always, whatever the curve, as Tamber Plug's AU publishes.
    Plain
};

// An AU parameter's unit, flags and value strings.
enum class AUUnits
{
    // Boolean, Indexed for a whole-number grid, Generic otherwise;
    // HighResolution, the curve's display flag and ValuesHaveStrings for a
    // choice or a bool (AU::fillParameterInfo).
    Native,

    // JUCE's AU (Compat::juceAUParameterInfo).
    Juce,

    // Tamber Plug's AU, bit for bit: Indexed when the parameter has steps (a
    // bool included), Generic otherwise; readable, writable and the CF name,
    // no other flag; no value strings in AU v2 or AUv3.
    Plug
};

// Which parameters the VST3 and AAX adapters publish as stepped, and their steps.
enum class StepRule
{
    // Every one with steps (Parameter::numSteps(): an int, a stepped float, a
    // choice, a bool): VST3 stepCount numSteps(), AAX Discrete with numSteps() + 1
    // steps. A continuous one has VST3 stepCount 0 and is AAX Continuous with the
    // SDK's default number of steps.
    NumSteps,

    // JUCE 8.0.12's wrappers: only a discrete one (Parameter::isDiscrete(), a
    // choice or a bool) is stepped, an int or a stepped float continuous, unless
    // juceLegacyAutomationType (Compat::juceVST3StepCount, Compat::juceAAXSteps).
    JuceDiscrete
};

// How the VST3 adapter lays out the hidden parameters a host maps MIDI
// controllers onto (IMidiMapping), when the plugin has them
// (vst3MidiControllersFor()).
enum class Vst3ControllerLayout
{
    // MakeASound's: base | bus << 12 | channel << 8 | controller, 131 per
    // channel (CC 0-127, aftertouch, pitch bend, program change) on each MIDI
    // input bus, each hidden and automatable.
    Native,

    // JUCE 8.0.12's (juce_audio_plugin_client_VST3.cpp, the managed-parameter
    // case): juceMidiControllerBase + channel * 130 + controller, CC 0-127 then
    // aftertouch and pitch bend, for one bus whatever the host asks, no program
    // change, registered with no flag (neither automatable nor hidden).
    JuceContiguous
};

// JUCE's first MIDI controller parameter id, 'mcm' << 8.
constexpr uint32_t juceMidiControllerBase = 0x6d636d00u;

struct Vst3Controllers
{
    Vst3ControllerLayout layout = Vst3ControllerLayout::Native;

    // Native's base. Plug's builds used 0x80000000 (VST3's host-reserved
    // range); MakeASound keeps to the plugin's 31 bits.
    uint32_t base = 0x7fff0000u;

    constexpr bool operator==(const Vst3Controllers&) const = default;
};

// The half of a plugin's identity its ParameterList computes and the AU and
// AUv3 adapters publish: the parameter rules of the resolved IdentityPolicy.
// PluginWrapper applies it, from the plugin's PluginDescription
// (identityPolicyFor()). The defaults are MakeASound's own.
struct ParameterIdentity
{
    ParamIdRule paramIds = ParamIdRule::PathHash;

    // JUCE_USE_STUDIO_ONE_COMPATIBLE_PARAMETERS (1, JUCE's default): JUCE masks
    // its VST3 and AU ids to 31 bits. Off, a host id may be 2^31 or more. Read
    // only under ParamIdRule::JuceKeyHash.
    bool juceMask31 = true;

    // Under ParamIdRule::Index, the last versionHint whose entries are indexed;
    // a later one's are post-port (ParameterList::Entry::postPort). Every entry
    // is indexed by default, which keeps a pure append safe; the first release
    // that adds a parameter anywhere else sets it to the hint the port shipped
    // with (IdentityOverrides) and gives the new parameter a higher one.
    int indexedThroughVersion = std::numeric_limits<int>::max();

    Registration registration = Registration::ExposedOnly;

    // JUCE's own id checks (duplicate keys, truncated AAX ids, group ids) in
    // ParameterList::identityProblems().
    bool juceChecks = false;

    AddedBypassFor addedBypassFor = AddedBypassFor::Effects;
    AddedBypassId addedBypassId = AddedBypassId::PathHash;
    BypassPlacement addedBypassInAU = BypassPlacement::Listed;

    // The declared bypass (a parameter with {.bypass = true}) in an AU list.
    // Every profile lists it; IdentityOverrides::declaredBypassInAU unlists it.
    BypassPlacement declaredBypassInAU = BypassPlacement::Listed;

    // When set, every entry's hostKey is its enclosing groups' own ids,
    // outermost first, each followed by this, then the parameter's hostKey():
    // "Drive-Filter-EQ-Low" for "Low" in "EQ" in "Drive-Filter" under '-'. A
    // root-level entry and the added bypass keep their own key. Every rule that
    // reads the hostKey (JuceKeyHash, the AUv3 identifier and hash, the AAX
    // KeyOrOverride id, JUCE's duplicate checks) sees the prefixed one. For a
    // JUCE port whose ids carried their groups' prefix. Under GroupIdRule::OwnId
    // the groups' ids are joined the same way (JUCE's group ids).
    std::optional<char> juceKeyGroupSeparator = {};

    AUv3Ids auv3 = AUv3Ids::HostIdAndPath;
    AAXIdRule aaxIds = AAXIdRule::HexHostId;
    GroupIdRule groups = GroupIdRule::Path;

    // Whether the VST3 adapter publishes the named groups as IUnitInfo units
    // (a root unit plus one per group, each parameter's unitId its innermost
    // group's) and the AU v2 adapter as clumps (kAudioUnitParameterFlag_HasClump,
    // kAudioUnitProperty_ParameterClumpName). Off, every parameter is in the
    // root unit and none has a clump, as Tamber Plug's formats published them.
    // The AUv3 tree has its groups either way.
    bool hostGroups = true;
    AUOrder auOrder = AUOrder::HintThenId;
    AUValueSpace auValues = AUValueSpace::NativeCurve;
    AUUnits auUnits = AUUnits::Native;

    // The VST3 stepCount and the AAX type and number of steps.
    StepRule steps = StepRule::NumSteps;

    // JUCE_FORCE_LEGACY_PARAMETER_AUTOMATION_TYPE, for JuceScaled values, JUCE
    // units and JuceDiscrete steps.
    bool juceLegacyAutomationType = false;

    // Not an id, but here since this half is what reaches the plugin: whether
    // PluginWrapper loads a ForeignOrigin::TamberPlugAU document as a native one
    // when the plugin's loadForeignState declines it. A port that kept Plug's
    // ids and plain ranges has Plug's schema, so needs no loadLegacy for it.
    bool foreignPlugStateIsNative = false;

    // The VST3 adapter's MIDI controller parameters: their layout, and whether
    // there are any (vst3MidiControllersFor() of the description, which
    // identityPolicyFor() fills in). A plugin parameter whose id lands among
    // them wins over the controller; identityProblems() lists it.
    Vst3Controllers vst3Controllers = {};
    bool vst3MidiControllers = false;

    // Whether a pinned host id (ParameterOptions::hostId) may use all 32 bits.
    // Off, one at 2^31 or above is reported, since VST3 reserves that range for
    // the host. On for foreign ids: a hosted plugin's parameters carry the ids the
    // plugin itself chose (a VST3 ParamID, an AudioUnitParameterID), which this
    // list must neither re-derive nor refuse (hostedPluginIdentity()).
    bool foreignHostIds = false;

    constexpr bool operator==(const ParameterIdentity&) const = default;
};

// The rules for a hosted plugin's parameter list: every parameter pins the
// foreign plugin's own id as its host id, any of the 32 bits, and every one is
// listed, as the plugin listed them. Nothing is derived, so no profile's hashing
// or indexing applies; the id paths, the state keys, are whatever the hosting
// format chose (the id in decimal, say).
constexpr ParameterIdentity hostedPluginIdentity() noexcept
{
    auto identity = ParameterIdentity {};
    identity.registration = Registration::All;
    identity.foreignHostIds = true;
    return identity;
}

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
