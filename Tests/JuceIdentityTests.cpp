// The ids a host stores for a plugin and its parameters (WP10.19, WP10.20,
// WP10.17): MakeASound's scheme kept as it was, and the JUCE schemes against
// values JUCE 8.0.12 computed. The expected numbers were printed by a throwaway
// JUCE console program calling VST3ClientExtensions::convertJuceParameterId,
// String::hashCode64, VST3Interface::jucePluginId and vst2PluginId,
// AAXClientExtensions::getPluginIDForMainBusConfig, and AudioParameterFloat,
// Choice, Bool and Int's isDiscrete, getNumSteps and getDefaultValue (the AU
// wrapper's range is getNumSteps() - 1 for a discrete parameter, 1 otherwise).

#include "TestPlugins.h"

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

using namespace nano;
using namespace MakeASound;

namespace
{

bool near(float a, float b, float tolerance = 1e-6f)
{
    return std::fabs(a - b) <= tolerance;
}

// The JUCE fixture's parameters, ids as a JUCE ParameterID would name them, the
// envelope two groups deep so a path and a leaf id differ.
struct EnvelopeParams : ParameterGroup
{
    EnvelopeParams()
        : ParameterGroup("Envelope", {.id = "env"})
    {
        add(attack);
    }

    FloatParam attack {"Attack", 0.f, 5.f, 0.01f, {.id = "attack"}};
};

struct VoiceParams : ParameterGroup
{
    VoiceParams()
        : ParameterGroup("Voice", {.id = "voice"})
    {
        add(envelope);
    }

    EnvelopeParams envelope;
};

struct FixtureParams : ParameterGroup
{
    FixtureParams() { add(cutoff, mode, enabled, voices, voice); }

    FloatParam cutoff {
        "Cutoff", FloatRange {20.f, 20000.f, 0.f, 0.3f}, 1000.f, {.id = "cutoff"}};
    ChoiceParam mode {"Mode", {"Clean", "Warm", "Hot"}, 1, {.id = "mode"}};
    BoolParam enabled {"Enabled", true, {.id = "enabled"}};
    IntParam voices {"Voices", 1, 16, 8, {.id = "voices"}};
    VoiceParams voice;
};

constexpr auto juce = policyFor(IdScheme::JUCE, {}).parameters;
constexpr auto juceUnmasked =
    policyFor(IdScheme::JUCE, {.studioOneCompatibleParameters = false}).parameters;
constexpr auto juceLegacy = policyFor(IdScheme::JUCELegacyIndex, {}).parameters;

// JUCE's per-id numbers: convertJuceParameterId(id, true), (id, false) and
// hashCode64.
struct JuceIds
{
    const char* id;
    uint32_t masked;
    uint32_t unmasked;
    uint64_t address;
};

constexpr JuceIds fixtureIds[] = {
    {"cutoff", 798419949u, 2945903597u, 1052795674147ull},
    {"mode", 3357091u, 3357091u, 113445321ull},
    {"enabled", 537889601u, 2685373249u, 108379842206215ull},
    {"voices", 1336493441u, 3483977089u, 1251851765449ull},
    {"attack", 740224584u, 2887708232u, 1031671269632ull},
};

// MakeASound's scheme, byte for byte as the adapters computed it before.
auto tMakeASound = test("Parameters/theMakeASoundSchemeIsUnchanged") = []
{
    auto params = FixtureParams {};
    auto list = ParameterList {params};

    check(list.identity() == ParameterIdentity {});

    for (const auto& entry: list)
    {
        check(entry.hostId == hostIdFor(entry.id));
        check(entry.address == entry.hostId);
        check(entry.identifier == entry.id);
        check(entry.listedInAU && !entry.addedBypass);
    }

    check(list.entry(4).id == "voice/env/attack");
    check(list.entry(4).hostKey == "attack");

    // The AAX id: eight lowercase hex digits of the host id.
    char hex[9] {};
    std::snprintf(hex, sizeof(hex), "%08x", list.entry(0).hostId);
    check(list.entry(0).aaxId == hex);

    const auto& groups = list.entry(4).groups;
    check(groups.size() == 2);
    check(groups[0].identifier == "voice" && groups[0].ownId == "voice");
    check(groups[1].identifier == "voice/env" && groups[1].ownId == "env");

    // Setting the default identity again moves nothing.
    auto before = list.entry(4).hostId;
    list.setIdentity({});
    check(list.entry(4).hostId == before);
    check(list.entry(4).aaxId == ParameterList {params}.entry(4).aaxId);
};

auto tHostKey = test("Parameters/aHostKeyDefaultsToTheLeafId") = []
{
    auto attack = FloatParam {"Attack"};
    auto pinned = FloatParam {"Release", 0.f, 1.f, 0.5f, {.hostKey = "rel"}};
    auto named = FloatParam {"Decay", 0.f, 1.f, 0.5f, {.id = "dec"}};

    check(attack.hostKey() == "Attack");
    check(pinned.hostKey() == "rel");
    check(named.hostKey() == "dec");
    check(pinned.explicitAAXId().empty());

    // MakeASound's scheme ignores the key: the host id is still the path's.
    struct Params : ParameterGroup
    {
        Params() { add(release); }
        FloatParam release {"Release", 0.f, 1.f, 0.5f, {.hostKey = "rel"}};
    };

    auto params = Params {};
    check(ParameterList {params}.entry(0).hostId == hostIdFor("Release"));
};

auto tLookups = test("Parameters/entriesAreFoundByAddressAndAAXId") = []
{
    auto params = FixtureParams {};

    for (auto identity: {ParameterIdentity {}, juce, juceLegacy})
    {
        auto list = ParameterList {params, identity};

        for (auto i = 0; i < list.size(); ++i)
        {
            check(list.indexOfHostId(list.entry(i).hostId) == i);
            check(list.indexOfAddress(list.entry(i).address) == i);
            check(list.indexOfAAXId(list.entry(i).aaxId) == i);
        }

        check(list.indexOfAddress(7777777ull) == -1);
        check(list.indexOfAAXId("nothing") == -1);
    }
};

auto tDiscrete = test("Parameters/discreteAndBooleanAreJuces") = []
{
    auto params = FixtureParams {};
    auto stepped = FloatParam {"Stepped", FloatRange {0.f, 10.f, 1.f}, 4.f};

    // JUCE: a choice and a bool are discrete, an int and a stepped float not.
    check(params.mode.isDiscrete() && !params.mode.isBoolean());
    check(params.enabled.isDiscrete() && params.enabled.isBoolean());
    check(!params.voices.isDiscrete() && !params.voices.isBoolean());
    check(!params.cutoff.isDiscrete());
    check(!stepped.isDiscrete() && stepped.numSteps() == 10);
};

auto tChoiceRounding = test("Parameters/aChoiceRoundsHalfToEven") = []
{
    auto choice = ChoiceParam {"Mode", {"A", "B", "C", "D"}};

    // juce::roundToInt, as an int does and as the normalized path always did.
    choice.setValue(0.5f);
    check(choice.getIndex() == 0);
    choice.setValue(1.5f);
    check(choice.getIndex() == 2);
    choice.setValue(2.5f);
    check(choice.getIndex() == 2);
    choice.setValue(2.6f);
    check(choice.getIndex() == 3);
    choice.setValue(1e9f);
    check(choice.getIndex() == 3);
    choice.setValue(-3.f);
    check(choice.getIndex() == 0);

    check(choice.valueToText(0.5f) == "A");
    check(choice.valueToText(1.5f) == "C");
};

auto tJuceIds = test("JuceCompat/parameterIdsAreJuces") = []
{
    auto params = FixtureParams {};
    auto list = ParameterList {params, juce};

    check(list.size() == 5);

    for (auto i = 0; i < list.size(); ++i)
    {
        const auto& entry = list.entry(i);
        const auto& expected = fixtureIds[i];

        check(entry.hostKey == expected.id);
        check(entry.hostId == expected.masked);
        check(entry.address == expected.address);

        // The AUv3 identifier and the AAX id are the key itself.
        check(entry.identifier == expected.id);
        check(entry.aaxId == expected.id);
        check(entry.listedInAU);
    }

    // Saved state keeps its own key, the id path.
    check(list.entry(4).id == "voice/env/attack");

    // A group's AUv3 identifier is its own id, as JUCE's tree names it.
    const auto& groups = list.entry(4).groups;
    check(groups[0].identifier == "voice");
    check(groups[1].identifier == "env");
    check(groups[1].id == "voice/env");
};

auto tUnmasked = test("JuceCompat/withoutTheStudioOneMaskIdsKeepBit31") = []
{
    auto params = FixtureParams {};
    auto list = ParameterList {params, juceUnmasked};

    for (auto i = 0; i < list.size(); ++i)
    {
        check(list.entry(i).hostId == fixtureIds[i].unmasked);
        check(list.entry(i).address == fixtureIds[i].address);
        check(list.indexOfHostId(fixtureIds[i].unmasked) == i);
    }

    // A host id pinned past 2^31 is allowed here, and found.
    struct Params : ParameterGroup
    {
        Params() { add(level); }
        FloatParam level {"Level", 0.f, 1.f, 0.5f, {.hostId = 0x90000001u}};
    };

    auto pinned = Params {};
    auto pinnedList = ParameterList {pinned};
    pinnedList.setIdentity(juceUnmasked);
    check(pinnedList.entry(0).hostId == 0x90000001u);
    check(pinnedList.indexOfHostId(0x90000001u) == 0);
};

auto tLegacy = test("JuceCompat/legacyIdsAreIndices") = []
{
    auto params = FixtureParams {};
    auto list = ParameterList {params, juceLegacy};

    for (auto i = 0; i < list.size(); ++i)
    {
        check(list.entry(i).hostId == static_cast<uint32_t>(i));
        check(list.entry(i).address == static_cast<uint64_t>(i));
        check(list.entry(i).aaxId == std::to_string(i));

        // AUv3 still identifies a parameter by its JUCE id.
        check(list.entry(i).identifier == fixtureIds[i].id);
    }
};

auto tAAXOverride = test("JuceCompat/anAAXIdOverridesTheKey") = []
{
    struct Params : ParameterGroup
    {
        Params() { add(level, drive); }
        FloatParam level {"Level", 0.f, 1.f, 0.5f, {.id = "level", .aaxId = "Lvl"}};
        FloatParam drive {
            "Drive", 0.f, 1.f, 0.5f, {.id = "drv", .hostKey = "drive"}};
    };

    auto params = Params {};
    auto list = ParameterList {params, juce};

    check(list.entry(0).aaxId == "Lvl");
    check(list.entry(0).hostKey == "level");
    check(list.entry(1).aaxId == "drive");
    check(list.entry(1).identifier == "drive");
    check(list.entry(1).id == "drv");

    // MakeASound's scheme ignores both.
    auto native = ParameterList {params};
    check(native.entry(0).hostId == hostIdFor("level"));
    check(native.entry(1).identifier == "drv");
};

// The wrapper's own parameters (WP10.20).
auto tAddedBypass = test("JuceCompat/theAddedBypassTakesJucesIds") = []
{
    using TestPlugins::UnbypassedGainPlugin;

    auto bypassOf = [](PluginWrapper& wrapper) -> const ParameterList::Entry&
    {
        const auto& list = wrapper.plugin().parameters();
        return list.entry(list.bypassIndex());
    };

    auto native = PluginWrapper(EA::makeOwned<UnbypassedGainPlugin>());
    check(bypassOf(native).addedBypass);
    check(bypassOf(native).hostId == 0x54D1526Fu);
    check(bypassOf(native).aaxId == "54d1526f");
    check(bypassOf(native).listedInAU);

    auto ported = PluginWrapper(
        EA::makeOwned<UnbypassedGainPlugin>(), PluginFormat::VST3, juce);
    const auto& list = ported.plugin().parameters();
    check(bypassOf(ported).hostId == juceVst3BypassId);
    check(bypassOf(ported).hostId == 0x62797073u);
    check(bypassOf(ported).aaxId == "MasterBypassID");
    check(!bypassOf(ported).listedInAU);
    check(list.indexOfHostId(juceVst3BypassId) == list.bypassIndex());

    // Its state key stays the wrapper's own: identity names hosts' ids only.
    check(bypassOf(ported).id == PluginWrapper::bypassStateKey);

    // The others are JUCE's hashes of their keys ("Gain" printed by JUCE).
    check(list.entry(0).hostId == 2211743u);

    auto legacy = PluginWrapper(
        EA::makeOwned<UnbypassedGainPlugin>(), PluginFormat::AAX, juceLegacy);
    const auto& legacyList = legacy.plugin().parameters();
    check(legacyList.bypassIndex() == legacyList.size() - 1);
    check(bypassOf(legacy).hostId == static_cast<uint32_t>(legacyList.size() - 1));
    check(bypassOf(legacy).aaxId == "MasterBypassID");
    check(!bypassOf(legacy).listedInAU);
};

auto tOwnBypass = test("JuceCompat/aDeclaredBypassIsTheMasterBypass") = []
{
    auto ported = PluginWrapper(
        EA::makeOwned<TestPlugins::GainPlugin>(), PluginFormat::AAX, juce);
    const auto& list = ported.plugin().parameters();
    const auto& bypass = list.entry(list.bypassIndex());

    // JUCE's AAX names any bypass the master one; VST3 and AU hash its key, and
    // AU lists it as it lists every parameter ("Bypass" printed by JUCE).
    check(!bypass.addedBypass);
    check(bypass.aaxId == "MasterBypassID");
    check(bypass.hostId == 2004703496u);
    check(bypass.listedInAU);
};

struct RebuildingEffect : TestPlugins::LatentEffect
{
    void rebuild() { setParameters(ParameterList {params}); }
};

auto tRebuilt = test("JuceCompat/aListSetAgainKeepsTheIdentity") = []
{
    auto wrapper =
        PluginWrapper(EA::makeOwned<RebuildingEffect>(), PluginFormat::VST3, juce);
    auto& plugin = static_cast<RebuildingEffect&>(wrapper.plugin());

    plugin.rebuild();

    const auto& list = plugin.parameters();
    check(list.identity() == juce);
    check(list.entry(0).hostId == 2211743u);
    check(list.entry(list.bypassIndex()).hostId == juceVst3BypassId);
};

// The class ids (WP10.20). JUCE's macOS bytes, as jucePluginId and vst2PluginId
// returned them.
// clang-format off
constexpr std::array<uint8_t, 16> juceComponentMaSoJcmp = {
    0xab, 0xcd, 0xef, 0x01, 0x91, 0x82, 0xfa, 0xeb,
    0x4d, 0x61, 0x53, 0x6f, 0x4a, 0x63, 0x6d, 0x70};
constexpr std::array<uint8_t, 16> vst2JcmpFixture = {
    0x56, 0x53, 0x54, 0x4a, 0x63, 0x6d, 0x70, 0x6a,
    0x75, 0x63, 0x65, 0x20, 0x63, 0x6f, 0x6d, 0x70};
constexpr std::array<uint8_t, 16> vst2GainGain = {
    0x56, 0x53, 0x54, 0x47, 0x61, 0x69, 0x6e, 0x67,
    0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<uint8_t, 16> vst2JcmpEmpty = {
    0x56, 0x53, 0x54, 0x4a, 0x63, 0x6d, 0x70, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
// clang-format on

// JUCE's Windows bytes for an id: jucePluginId reads its words LSB first there,
// and vst2PluginId swaps 0-3, 1-2, 4-5 and 6-7; both come to this.
std::array<uint8_t, 16> juceWindowsBytes(std::array<uint8_t, 16> bytes)
{
    std::swap(bytes[0], bytes[3]);
    std::swap(bytes[1], bytes[2]);
    std::swap(bytes[4], bytes[5]);
    std::swap(bytes[6], bytes[7]);
    return bytes;
}

auto tVst3ClassIds = test("JuceCompat/vst3ClassIdsAreJuces") = []
{
    using Compat::juceVst2ClassId;
    using Compat::juceVst3ClassId;

    auto component = juceVst3ClassId(FourCC("MaSo"), FourCC("Jcmp"));
    check(component
          == Vst3ClassId {0xABCDEF01u, 0x9182FAEBu, 0x4d61536fu, 0x4a636d70u});
    check(vst3ClassIdBytes(component, TuidLayout::Plain) == juceComponentMaSoJcmp);
    check(vst3ClassIdBytes(component, TuidLayout::COM)
          == juceWindowsBytes(juceComponentMaSoJcmp));

    auto fixture = juceVst2ClassId(FourCC("Jcmp").toUint32(), "Juce Compat Fixture");
    check(vst3ClassIdBytes(fixture, TuidLayout::Plain) == vst2JcmpFixture);
    check(vst3ClassIdBytes(fixture, TuidLayout::COM)
          == juceWindowsBytes(vst2JcmpFixture));

    auto gain = juceVst2ClassId(FourCC("Gain").toUint32(), "Gain");
    check(vst3ClassIdBytes(gain, TuidLayout::Plain) == vst2GainGain);
    check(vst3ClassIdBytes(juceVst2ClassId(FourCC("Jcmp").toUint32(), ""),
                           TuidLayout::Plain)
          == vst2JcmpEmpty);
};

auto tVst3ClassFor = test("JuceCompat/aDescriptionNamesItsVst3Class") = []
{
    auto module =
        ModuleDescription {.vendor = "Fixtures", .manufacturerCode = FourCC("MaSo")};
    auto plugin = PluginDescription {.name = "Juce Compat Fixture",
                                     .pluginCode = FourCC("Jcmp")};

    // MakeASound's own, as the VST3 factory derives it.
    check(vst3ClassIdFor(module, plugin)
          == Vst3ClassId {0x4d61536fu, 0x4a636d70u, FourCC("VST3").toUint32(), 0u});

    // JUCE's default replaces the VST2 class.
    plugin.idScheme = IdScheme::JUCE;
    check(vst3ClassIdBytes(vst3ClassIdFor(module, plugin), TuidLayout::Plain)
          == vst2JcmpFixture);

    // Its name and VST2 id are JucePlugin_Name's and JucePlugin_VSTUniqueID's.
    plugin.name = "Other";
    plugin.juce.pluginName = "Juce Compat Fixture";
    check(vst3ClassIdBytes(vst3ClassIdFor(module, plugin), TuidLayout::Plain)
          == vst2JcmpFixture);
    plugin.juce.vstUniqueId = FourCC("Gain");
    plugin.juce.pluginName = "Gain";
    check(vst3ClassIdBytes(vst3ClassIdFor(module, plugin), TuidLayout::Plain)
          == vst2GainGain);

    plugin.juce.vst3CanReplaceVst2 = false;
    check(vst3ClassIdBytes(vst3ClassIdFor(module, plugin), TuidLayout::Plain)
          == juceComponentMaSoJcmp);

    plugin.idScheme = IdScheme::JUCELegacyIndex;
    check(vst3ClassIdBytes(vst3ClassIdFor(module, plugin), TuidLayout::Plain)
          == juceComponentMaSoJcmp);

    // An override wins over every scheme.
    plugin.vst3ClassId = Vst3ClassId {1, 2, 3, 4};
    check(vst3ClassIdFor(module, plugin) == Vst3ClassId {1, 2, 3, 4});
};

auto tAAXPluginIds = test("JuceCompat/aaxNativeIdsAreJuces") = []
{
    using Compat::juceAAXPluginId;

    // getPluginIDForMainBusConfig over disabled, mono and stereo each way, as
    // JUCE printed it; AudioSuite's from 'jyaa'.
    constexpr uint32_t native[3][3] = {{0x6a636161u, 0x6a636162u, 0x6a636163u},
                                       {0x6a636261u, 0x6a636262u, 0x6a636263u},
                                       {0x6a636361u, 0x6a636362u, 0x6a636363u}};
    constexpr uint32_t suite[3][3] = {{0x6a796161u, 0x6a796162u, 0x6a796163u},
                                      {0x6a796261u, 0x6a796262u, 0x6a796263u},
                                      {0x6a796361u, 0x6a796362u, 0x6a796363u}};

    for (auto in = 0; in < 3; ++in)
        for (auto out = 0; out < 3; ++out)
        {
            check(juceAAXPluginId(in, out) == native[in][out]);
            check(juceAAXPluginId(in, out, true) == suite[in][out]);
        }

    // A set JUCE does not list leaves its letter alone.
    check(juceAAXPluginId(-1, 2) == 0x6a636163u);
    check(Compat::juceAAXChannelSetIndex(6) == -1);
};

auto tAAXNativeFor = test("JuceCompat/aDescriptionNamesItsAAXComponents") = []
{
    auto plugin = PluginDescription {.name = "Gain", .pluginCode = FourCC("Gain")};
    auto monoMono = AAXStems {1, 1, 0};
    auto stereoStereo = AAXStems {2, 2, 2};
    auto noneStereo = AAXStems {0, 2, 4};

    // MakeASound's own: the code plus the stem pair's index.
    check(aaxNativeIdFor(plugin, monoMono) == FourCC("Gain").toUint32());
    check(aaxNativeIdFor(plugin, stereoStereo) == FourCC("Gain").toUint32() + 2);

    plugin.idScheme = IdScheme::JUCE;
    check(aaxNativeIdFor(plugin, monoMono) == FourCC("jcbb").toUint32());
    check(aaxNativeIdFor(plugin, AAXStems {1, 2, 1}) == FourCC("jcbc").toUint32());
    check(aaxNativeIdFor(plugin, stereoStereo) == FourCC("jccc").toUint32());
    check(aaxNativeIdFor(plugin, noneStereo) == FourCC("jcac").toUint32());

    // The hook wins.
    plugin.aaxNativeId = [](const AAXStems& stems)
    {
        return 1000u * static_cast<uint32_t>(stems.inputChannels)
               + stems.outputChannels;
    };
    check(aaxNativeIdFor(plugin, AAXStems {1, 2, 1}) == 1002u);
};

auto tAAXEffectFor = test("JuceCompat/aDescriptionNamesItsAAXEffect") = []
{
    auto plugin = PluginDescription {.name = "Gain", .pluginCode = FourCC("Gain")};

    check(aaxEffectIdFor("com.makeasound.gain", plugin)
          == "com.makeasound.gain.Gain");

    // JUCE's JucePlugin_AAXIdentifier defaults to the bundle id.
    plugin.idScheme = IdScheme::JUCE;
    check(aaxEffectIdFor("com.makeasound.gain", plugin) == "com.makeasound.gain");

    plugin.aaxEffectId = "com.vendor.oldgain";
    check(aaxEffectIdFor("com.makeasound.gain", plugin) == "com.vendor.oldgain");
};

auto tAURange = test("JuceCompat/theAURangeIsJuces") = []
{
    using Compat::juceAUParameterInfo;

    auto params = FixtureParams {};
    auto stepped = FloatParam {"Stepped", FloatRange {0.f, 10.f, 1.f}, 4.f};
    auto settings = JuceSettings {};

    // Continuous: 0..1, the normalized default, high resolution and rampable.
    auto cutoff = juceAUParameterInfo(params.cutoff, true, IdScheme::JUCE, settings);
    check(cutoff.minValue == 0.f && cutoff.maxValue == 1.f);
    check(near(cutoff.defaultValue, 0.404752135f));
    check(!cutoff.discrete && !cutoff.indexedUnit && cutoff.canRamp);
    check(cutoff.highResolution && !cutoff.nonRealTime);
    check(!cutoff.v2ValueStrings && !cutoff.v3ValueStrings);

    // An int and a stepped float are continuous to JUCE.
    auto voices = juceAUParameterInfo(params.voices, true, IdScheme::JUCE, settings);
    check(!voices.discrete && voices.maxValue == 1.f);
    check(near(voices.defaultValue, 0.466666669f));

    auto grid = juceAUParameterInfo(stepped, true, IdScheme::JUCE, settings);
    check(!grid.discrete && grid.maxValue == 1.f);
    check(near(grid.defaultValue, 0.400000006f));

    // A choice: 0..choices-1, Indexed, the value strings.
    auto mode = juceAUParameterInfo(params.mode, true, IdScheme::JUCE, settings);
    check(mode.discrete && !mode.boolean && mode.indexedUnit);
    check(mode.maxValue == 2.f && mode.defaultValue == 1.f && !mode.canRamp);
    check(mode.v2ValueStrings && mode.v3ValueStrings);
    check(mode.toHost(params.mode.toNormalized(2.f)) == 2.f);
    check(near(mode.toNormalized(1.f), 0.5f));

    // A bool: 0..1, Boolean.
    auto enabled =
        juceAUParameterInfo(params.enabled, true, IdScheme::JUCE, settings);
    check(enabled.discrete && enabled.boolean && !enabled.indexedUnit);
    check(enabled.maxValue == 1.f && enabled.defaultValue == 1.f);

    // Not automatable: still listed, NonRealTime.
    check(juceAUParameterInfo(params.cutoff, false, IdScheme::JUCE, settings)
              .nonRealTime);

    // Legacy ids: the v2 unit answers no value strings; the AUv3 still has them.
    auto legacyMode =
        juceAUParameterInfo(params.mode, true, IdScheme::JUCELegacyIndex, settings);
    check(!legacyMode.v2ValueStrings && legacyMode.v3ValueStrings);

    // JUCE_FORCE_LEGACY_PARAMETER_AUTOMATION_TYPE: every parameter 0..1, none
    // Indexed, none high resolution.
    settings.forceLegacyParameterAutomationType = true;
    auto oldMode = juceAUParameterInfo(params.mode, true, IdScheme::JUCE, settings);
    check(oldMode.maxValue == 1.f && near(oldMode.defaultValue, 0.5f));
    check(!oldMode.indexedUnit && !oldMode.highResolution
          && !oldMode.v3ValueStrings);
};

auto tIdentityFor = test("JuceCompat/aDescriptionGivesItsParameterIdentity") = []
{
    auto plugin = PluginDescription {.name = "Gain", .pluginCode = FourCC("Gain")};
    check(parameterIdentityFor(plugin) == ParameterIdentity {});

    plugin.idScheme = IdScheme::JUCE;
    plugin.juce.studioOneCompatibleParameters = false;
    check(parameterIdentityFor(plugin) == juceUnmasked);
};

// The profiles resolve at compile time, and only the profile and JUCE's
// settings decide the rules.
static_assert(policyFor(IdScheme::MakeASound, {}) == IdentityPolicy {});
static_assert(policyFor(IdScheme::JUCE, {}).vst3Class == Vst3ClassRule::JuceVst2);
static_assert(policyFor(IdScheme::JUCE, {.vst3CanReplaceVst2 = false}).vst3Class
              == Vst3ClassRule::JuceVst3);

auto tPolicies = test("JuceCompat/eachProfileResolvesToItsRules") = []
{
    auto native = policyFor(IdScheme::MakeASound, {.vst3CanReplaceVst2 = false});
    check(native == IdentityPolicy {});

    auto modern = policyFor(IdScheme::JUCE, {}).parameters;
    check(modern.paramIds == ParamIdRule::JuceKeyHash);
    check(modern.addedBypassId == AddedBypassId::JuceByps);
    check(modern.addedBypassInAU == BypassPlacement::Unlisted);
    check(modern.auOrder == AUOrder::HintThenId);

    auto legacy = policyFor(IdScheme::JUCELegacyIndex, {}).parameters;
    check(legacy.paramIds == ParamIdRule::Index);
    check(legacy.addedBypassId == AddedBypassId::Index);
    check(legacy.auOrder == AUOrder::SdkIndexed);
    check(legacy.aaxIds == AAXIdRule::DecimalIndex);

    auto plugin = PluginDescription {.name = "Port",
                                     .pluginCode = FourCC("Port"),
                                     .idScheme = IdScheme::JUCELegacyIndex};
    check(identityPolicyFor(plugin) == policyFor(IdScheme::JUCELegacyIndex, {}));
};

} // namespace
