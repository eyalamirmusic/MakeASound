#pragma once

// A JUCE plugin ported under its JUCE build's ids, for the AU and AUv3 suites:
// the parameters of JuceIdentityTests' fixture (a cutoff skewed 0.3, a choice of
// three at 1, a bool, an int 1..16 at 8, an attack inside a group) plus one that
// is not automatable, an effect with no bypass of its own, and the two
// descriptions it ships under, IdScheme::JUCE (with an auType of 'aumf', as a
// JUCE effect that wants MIDI registers) and JUCELegacyIndex. No format header:
// each suite hosts it its own way, the v2 one through the factory the test
// module exports, MakeASoundAUTestPortFactory.

#include "JuceFixtures.h"
#include "TestPlugins.h"

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <array>
#include <cstdint>

namespace JucePort
{
using namespace MakeASound;

// The same group JuceFixtures' state fixture declares.
using JuceFixtures::EnvelopeParams;

struct PortParams : ParameterGroup
{
    PortParams() { add(cutoff, mode, enabled, voices, env, quality); }

    FloatParam cutoff {"Cutoff",
                       FloatRange {20.f, 20000.f, 0.f, 0.3f},
                       1000.f,
                       {.id = "cutoff", .versionHint = 1}};
    ChoiceParam mode {
        "Mode", {"Clean", "Warm", "Hot"}, 1, {.id = "mode", .versionHint = 1}};
    BoolParam enabled {"Enabled", true, {.id = "enabled", .versionHint = 1}};
    IntParam voices {"Voices", 1, 16, 8, {.id = "voices", .versionHint = 1}};
    EnvelopeParams env;

    // Listed by JUCE's AU all the same, NonRealTime; a later release's.
    FloatParam quality {"Quality",
                        0.f,
                        1.f,
                        0.5f,
                        {.id = "quality", .automatable = false, .versionHint = 2}};
};

// Reads its JUCE build's APVTS state, root "FixturePlugin" as JuceFixtures'.
struct PortPlugin : StatePlugin<State<PortParams>>
{
    std::string_view name() const override { return "Port"; }
    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    bool loadLegacy(const ForeignState& foreign) override
    {
        ++legacyLoads;
        return Compat::loadJuceApvtsBlob(
                   foreign.bytes, params, {.rootType = "FixturePlugin"})
            .has_value();
    }

    int legacyLoads = 0;
};

// JUCE 8.0.12's numbers for the fixture's ids (JuceIdentityTests' fixtureIds):
// String::hashCode masked to 31 bits, and hashCode64.
struct JuceIds
{
    const char* id;
    uint32_t hostId;
    uint64_t address;
};

constexpr JuceIds juceIds[] = {
    {"cutoff", 798419949u, 1052795674147ull},
    {"mode", 3357091u, 113445321ull},
    {"enabled", 537889601u, 108379842206215ull},
    {"voices", 1336493441u, 1251851765449ull},
    {"attack", 740224584u, 1031671269632ull},
};

// What JUCE 8.0.12 itself published for a twin of PortParams: a JUCE plugin
// built from ~/Code/JUCE (8.0.12-4-g501c07674e) as AU and VST3, 'aumf' MaSo
// Jprt with NEEDS_MIDI_INPUT, JUCE_VST3_CAN_REPLACE_VST2=0 and one program,
// once per JUCE_FORCE_USE_LEGACY_PARAM_IDS (Jlgy for 1), its APVTS the
// parameters above with the same ranges, hints and groups and no bypass. Read
// back from the built binaries by a throwaway AudioToolbox program (the AU
// registered in process: ParameterList, ParameterInfo) and a throwaway VST3 host
// (getParameterInfo, IMidiMapping).
struct JucePublishedAU
{
    const char* key;
    uint32_t id;
    float minValue;
    float maxValue;
    float defaultValue;
    uint32_t unit;
    uint32_t flags;
};

// In the AU's list order, (hint, id). JUCE's legacy build lists the same info
// by index 0..5 in declaration order, and answers no ParameterValueStrings
// though the flag says it has them.
constexpr JucePublishedAU juceAU[] = {
    {"mode", 3357091u, 0.f, 2.f, 1.f, 1, 0xc8a00010u},
    {"enabled", 537889601u, 0.f, 1.f, 1.f, 2, 0xc8a00010u},
    {"attack", 740224584u, 0.f, 1.f, 0.00199999986f, 0, 0xcab00010u},
    {"cutoff", 798419949u, 0.f, 1.f, 0.404752135f, 0, 0xcaa00010u},
    {"voices", 1336493441u, 0.f, 1.f, 0.466666669f, 0, 0xcaa00010u},
    {"quality", 651215103u, 0.f, 1.f, 0.5f, 0, 0xcba00010u},
};

// JUCE's AU clumps a grouped parameter (kAudioUnitParameterFlag_HasClump, the
// group's index + 1); MakeASound's AU has no clumps yet.
constexpr uint32_t juceAUHasClump = 0x00100000u;

struct JucePublishedVST3
{
    const char* key;
    uint32_t id;
    uint32_t legacyId;
    int stepCount;
    double defaultValue;
    int32_t flags;
};

// In registration order: declaration order, then the bypass JUCE's wrapper
// adds ('byps', or the next index under legacy ids). Attack's unit is its
// group's (hashCode 100589) in JUCE; MakeASound's VST3 units are WP10.10.
constexpr JucePublishedVST3 juceVST3[] = {
    {"cutoff", 798419949u, 0, 0, 0.404752135, 0x1},
    {"mode", 3357091u, 1, 2, 0.5, 0x1},
    {"enabled", 537889601u, 2, 1, 1.0, 0x1},
    {"voices", 1336493441u, 3, 0, 0.466666669, 0x1},
    {"attack", 740224584u, 4, 0, 0.00199999986, 0x1},
    {"quality", 651215103u, 5, 0, 0.5, 0x0},
    {"__bypass", 0x62797073u, 6, 1, 0.0, 0x10001},
};

// The component class id under JUCE_VST3_CAN_REPLACE_VST2=0.
constexpr std::array<uint32_t, 4> juceVST3ClassId = {
    0xABCDEF01u, 0x9182FAEBu, 0x4D61536Fu, 0x4A707274u};

constexpr auto manufacturer = FourCC("MaSo");

inline const PluginDescription& description(IdScheme scheme)
{
    static const auto juce = PluginDescription {
        .name = "Port",
        .pluginCode = "Jprt",
        .create = []
        { return TestPlugins::Module::create<PortPlugin>(PluginCreateFn {}); },
        .idScheme = IdScheme::JUCE,
        .auType = FourCC("aumf")};

    static const auto legacy = PluginDescription {
        .name = "Legacy Port",
        .pluginCode = "Jlgy",
        .create = []
        { return TestPlugins::Module::create<PortPlugin>(PluginCreateFn {}); },
        .idScheme = IdScheme::JUCELegacyIndex};

    return scheme == IdScheme::JUCELegacyIndex ? legacy : juce;
}

// Null for any other subtype.
inline const PluginDescription* descriptionFor(uint32_t subtype)
{
    for (auto scheme: {IdScheme::JUCE, IdScheme::JUCELegacyIndex})
        if (description(scheme).pluginCode.toUint32() == subtype)
            return &description(scheme);

    return nullptr;
}

} // namespace JucePort
