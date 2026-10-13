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
