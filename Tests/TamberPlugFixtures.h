#pragma once

// What a Tamber Plug build's AU v2 saved, shared by every format suite that hands
// its adapter a Plug session. Captured from Plug's own AU components (built from
// Tamber-Inc/Plug at 2457f60, installed in ~/Library/Audio/Plug-Ins/Components)
// by a throwaway AudioToolbox program: AudioComponentInstanceNew, the parameters
// set with AudioUnitSetParameter (Plug's AU ids are list indices, plain values),
// then kAudioUnitProperty_ClassInfo written out with NSPropertyListSerialization
// as XML. The dictionaries below are those files verbatim, so `TamberPlugState`
// (Plug's StateContext::Session document) and `data` (the SDK's parameter values
// under Plug's ids) are the bytes Plug wrote. Plug's VST3 getState writes the same
// document with no framing.
//
// SVFFilter (aufx Svf1 EAPM) has three nested lists, HP / BP / LP, each with a
// bool Enable, a Cutoff (Plug's FrequencyParam: a 0..1 position, not Hz) and a
// Resonance; saved with HP on at 0.4, BP resonance 0.65, LP off at 0.5. Chorus
// (aufx Chr1 EAPM) is flat, with two choices; saved with Input Level High, Gain
// 7.5, Mode Vibrato, Stereo Width 1.5, Mix 0.25 and Bypass on.
//
// SVFPort and ChorusPort are those plugins ported: the same ids, ranges and
// plain values, each taking Plug's document through loadLegacy.

#include "TestPlugins.h"

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace TamberPlugFixtures
{
using namespace MakeASound;

// clang-format off
inline constexpr auto svfClassInfo = std::string_view {R"plist(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>TamberPlugState</key>
	<data>
	eyJwYXJhbXMiOnsiQlAiOnsiQ3V0b2ZmIjowLjU2NjMyMzMzOTkzOTExNzQsIkVuYWJs
	ZSI6MCwiUmVzb25hbmNlIjowLjY0OTk5OTk3NjE1ODE0MjF9LCJIUCI6eyJDdXRvZmYi
	OjAuNDAwMDAwMDA1OTYwNDY0NSwiRW5hYmxlIjoxLCJSZXNvbmFuY2UiOjAuMjAwMDAw
	MDAyOTgwMjMyMjR9LCJMUCI6eyJDdXRvZmYiOjAuNSwiRW5hYmxlIjowLCJSZXNvbmFu
	Y2UiOjAuMjAwMDAwMDAyOTgwMjMyMjR9fSwidmVyc2lvbiI6MX0=
	</data>
	<key>data</key>
	<data>
	AAAAAAAAAAAAAAAJAAAAAD+AAAAAAAABPszMzQAAAAI+TMzNAAAAAwAAAAAAAAAEPxD6
	kQAAAAU/JmZmAAAABgAAAAAAAAAHPwAAAAAAAAg+TMzN
	</data>
	<key>manufacturer</key>
	<integer>1161908301</integer>
	<key>name</key>
	<string>Untitled</string>
	<key>subtype</key>
	<integer>1400268337</integer>
	<key>type</key>
	<integer>1635083896</integer>
	<key>version</key>
	<integer>0</integer>
</dict>
</plist>
)plist"};

inline constexpr auto chorusClassInfo = std::string_view {R"plist(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>TamberPlugState</key>
	<data>
	eyJwYXJhbXMiOnsiQnlwYXNzIjoxLCJHYWluIjo3LjUsIkhpZ2ggUGFzcyI6MCwiSW5w
	dXQgTGV2ZWwiOiJIaWdoIiwiSW50ZW5zaXR5Ijo1LCJMb3cgUGFzcyI6MSwiTWl4Ijow
	LjI1LCJNb2RlIjoiVmlicmF0byIsIlJhdGUiOjUsIlN0ZXJlbyBXaWR0aCI6MS41LCJW
	aWJyYXRvIERlcHRoIjo1fSwidmVyc2lvbiI6MX0=
	</data>
	<key>data</key>
	<data>
	AAAAAAAAAAAAAAALAAAAAD+AAAAAAAABQPAAAAAAAAIAAAAAAAAAA0CgAAAAAAAEQKAA
	AAAAAAVAoAAAAAAABj+AAAAAAAAHP8AAAAAAAAg/gAAAAAAACT6AAAAAAAAKP4AAAA==
	</data>
	<key>manufacturer</key>
	<integer>1161908301</integer>
	<key>name</key>
	<string>Untitled</string>
	<key>subtype</key>
	<integer>1130918449</integer>
	<key>type</key>
	<integer>1635083896</integer>
	<key>version</key>
	<integer>0</integer>
</dict>
</plist>
)plist"};

// The `TamberPlugState` of each, decoded: what Plug's VST3 getState writes for
// the same values.
inline constexpr auto svfDocument = std::string_view {
    R"({"params":{"BP":{"Cutoff":0.5663233399391174,"Enable":0,"Resonance":0.6499999761581421},"HP":{"Cutoff":0.4000000059604645,"Enable":1,"Resonance":0.20000000298023224},"LP":{"Cutoff":0.5,"Enable":0,"Resonance":0.20000000298023224}},"version":1})"};

inline constexpr auto chorusDocument = std::string_view {
    R"({"params":{"Bypass":1,"Gain":7.5,"High Pass":0,"Input Level":"High","Intensity":5,"Low Pass":1,"Mix":0.25,"Mode":"Vibrato","Rate":5,"Stereo Width":1.5,"Vibrato Depth":5},"version":1})"};
// clang-format on

// The AU ids Plug gave them: indices into the parameter list.
inline constexpr auto svfParameterCount = 9;
inline constexpr auto chorusParameterCount = 11;

// What Plug's own builds published, for a port under IdScheme::TamberPlug to
// match. Probed from the AU and VST3 bundles installed from ~/Code/Plug
// (2457f60, built 2026-10-13) by a throwaway AudioToolbox program (ParameterList
// and ParameterInfo after AudioUnitInitialize; auval passes both) and a
// throwaway VST3 host over the vendored SDK (getParameterInfo). Plug's AU list
// is ids 0..n-1 in order, every flag word Readable | Writable | HasCFNameString
// | CFNameRelease, no value strings, no BypassEffect; every VST3 parameter is
// kCanAutomate alone, in the root unit. TamberGestures (TAmb) shows the same
// scheme with 7 exposed ids among 2989 (1459, 1467, ... 1507), sparse.
inline constexpr uint32_t plugAUFlags = 0xC8000010u;

struct PlugParameter
{
    std::string_view name;
    float minValue;
    float maxValue;
    float defaultValue;

    // kAudioUnitParameterUnit_Indexed (1) where it has steps, else Generic (0).
    uint32_t auUnit;

    int vst3Steps;
    double vst3Default;
};

inline constexpr PlugParameter svfPublished[svfParameterCount] = {
    {"HP Enable", 0.f, 1.f, 0.f, 1, 1, 0.0},
    {"HP Cutoff", 0.f, 1.f, 0.23299f, 0, 0, 0.232989997},
    {"HP Resonance", 0.f, 1.f, 0.2f, 0, 0, 0.200000003},
    {"BP Enable", 0.f, 1.f, 0.f, 1, 1, 0.0},
    {"BP Cutoff", 0.f, 1.f, 0.566323f, 0, 0, 0.56632334},
    {"BP Resonance", 0.f, 1.f, 0.2f, 0, 0, 0.2},
    {"LP Enable", 0.f, 1.f, 1.f, 1, 1, 1.0},
    {"LP Cutoff", 0.f, 1.f, 0.725364f, 0, 0, 0.725363731},
    {"LP Resonance", 0.f, 1.f, 0.2f, 0, 0, 0.2},
};

inline constexpr PlugParameter chorusPublished[chorusParameterCount] = {
    {"Input Level", 0.f, 1.f, 0.f, 1, 1, 0.0},
    {"Gain", 0.f, 10.f, 5.f, 0, 0, 0.5},
    {"High Pass", 0.f, 1.f, 0.f, 0, 0, 0.0},
    {"Rate", 0.f, 10.f, 5.f, 0, 0, 0.5},
    {"Intensity", 0.f, 10.f, 5.f, 0, 0, 0.5},
    {"Vibrato Depth", 0.f, 10.f, 5.f, 0, 0, 0.5},
    {"Mode", 0.f, 1.f, 0.f, 1, 1, 0.0},
    {"Stereo Width", 0.f, 2.f, 1.f, 0, 0, 0.5},
    {"Low Pass", 0.f, 1.f, 1.f, 0, 0, 1.0},
    {"Mix", 0.f, 1.f, 1.f, 0, 0, 1.0},
    {"Bypass", 0.f, 1.f, 0.f, 1, 1, 0.0},
};

// Plug's VST3 class ids: FUID('EAPM', code, 'VST3', 0), MakeASound's own.
inline constexpr std::array<uint32_t, 4> svfClassId = {
    0x4541504Du, 0x53766631u, 0x56535433u, 0u};
inline constexpr std::array<uint32_t, 4> chorusClassId = {
    0x4541504Du, 0x43687231u, 0x56535433u, 0u};

inline bool plugNear(float a, float b, float tolerance = 1e-4f)
{
    return std::abs(a - b) < tolerance;
}

// One of SVFFilter's bands, mounted under its prefix.
struct SVFBand : ParameterGroup
{
    SVFBand(std::string_view prefix, bool enabledByDefault, float cutoffPosition)
        : ParameterGroup(prefix)
        , enabled {"Enable", enabledByDefault, {.versionHint = 1}}
        , cutoff {"Cutoff", 0.f, 1.f, cutoffPosition, {.versionHint = 1}}
    {
        add(enabled, cutoff, resonance);
    }

    BoolParam enabled;
    FloatParam cutoff;
    FloatParam resonance {"Resonance", 0.f, 1.f, 0.2f, {.versionHint = 1}};
};

// Plug's SVFFilterPluginParams, its defaults the positions of 100 Hz, 1 kHz and
// 3 kHz on its 20..20000 Hz curve.
struct SVFParams : ParameterGroup
{
    SVFParams() { add(highPass, bandPass, lowPass); }

    SVFBand highPass {"HP", false, 0.23298999667167664f};
    SVFBand bandPass {"BP", false, 0.5663233399391174f};
    SVFBand lowPass {"LP", true, 0.7253637313842773f};
};

inline bool holdsSVFValues(const SVFParams& params)
{
    return params.highPass.enabled.isOn()
           && plugNear(params.highPass.cutoff.get(), 0.4f)
           && plugNear(params.highPass.resonance.get(), 0.2f)
           && !params.bandPass.enabled.isOn()
           && plugNear(params.bandPass.cutoff.get(), 0.5663233f)
           && plugNear(params.bandPass.resonance.get(), 0.65f)
           && !params.lowPass.enabled.isOn()
           && plugNear(params.lowPass.cutoff.get(), 0.5f)
           && plugNear(params.lowPass.resonance.get(), 0.2f);
}

inline bool holdsSVFDefaults(const SVFParams& params)
{
    return !params.highPass.enabled.isOn() && !params.bandPass.enabled.isOn()
           && params.lowPass.enabled.isOn()
           && plugNear(params.highPass.cutoff.get(), 0.23299f)
           && plugNear(params.lowPass.cutoff.get(), 0.7253637f)
           && plugNear(params.bandPass.resonance.get(), 0.2f);
}

// Plug's ChorusParams. Its Bypass is the plugin's own, an ordinary bool: Plug's
// Parameter::isBypass() is never overridden, so neither format saw it as a
// bypass (no kIsBypass, no BypassEffect), and the wrapper adds one beside it.
struct ChorusParams : ParameterGroup
{
    ChorusParams()
    {
        add(inputLevel,
            gain,
            highPass,
            rate,
            intensity,
            vibratoDepth,
            mode,
            width,
            lowPass,
            mix,
            bypass);
    }

    ChoiceParam inputLevel {"Input Level", {"Low", "High"}, 0, {.versionHint = 1}};
    FloatParam gain {"Gain", 0.f, 10.f, 5.f, {.versionHint = 1}};
    FloatParam highPass {"High Pass", 0.f, 1.f, 0.f, {.versionHint = 1}};
    FloatParam rate {"Rate", 0.f, 10.f, 5.f, {.versionHint = 1}};
    FloatParam intensity {"Intensity", 0.f, 10.f, 5.f, {.versionHint = 1}};
    FloatParam vibratoDepth {"Vibrato Depth", 0.f, 10.f, 5.f, {.versionHint = 1}};
    ChoiceParam mode {"Mode", {"Chorus", "Vibrato"}, 0, {.versionHint = 1}};
    FloatParam width {"Stereo Width", 0.f, 2.f, 1.f, {.versionHint = 1}};
    FloatParam lowPass {"Low Pass", 0.f, 1.f, 1.f, {.versionHint = 1}};
    FloatParam mix {"Mix", 0.f, 1.f, 1.f, {.versionHint = 1}};
    BoolParam bypass {"Bypass", false, {.versionHint = 1}};
};

inline bool holdsChorusValues(const ChorusParams& params)
{
    return params.inputLevel.getIndex() == 1 && plugNear(params.gain.get(), 7.5f)
           && params.mode.getIndex() == 1 && plugNear(params.width.get(), 1.5f)
           && plugNear(params.mix.get(), 0.25f) && params.bypass.isOn()
           && plugNear(params.rate.get(), 5.f)
           && plugNear(params.lowPass.get(), 1.f);
}

// A port taking Plug's document as its own, which it is when the ids and plain
// values are Plug's. It records what each foreign load handed it, and refuses
// every one while `refuse` is set.
template <typename ParamsT>
struct PlugPort : StatePlugin<State<ParamsT>>
{
    std::string_view name() const override { return "PlugPort"; }
    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    bool loadLegacy(const ForeignState& foreign) override
    {
        ++legacyLoads;
        origin = foreign.origin;
        context = foreign.context;

        if (refuse || foreign.origin != ForeignOrigin::TamberPlugAU)
            return false;

        this->state.deserialize(foreign.bytes, foreign.context);
        return true;
    }

    std::atomic<int> legacyLoads {0};
    std::atomic<bool> refuse {false};
    ForeignOrigin origin = ForeignOrigin::JuceVST3;
    StateContext context = StateContext::Preset;
};

// No Plug example hides a parameter, but Plug's ids count one all the same
// (TamberGestures' 7 exposed ids sit among 2989): a hidden leaf between two
// exposed ones, so the second's id is 2.
struct SparseParams : ParameterGroup
{
    SparseParams() { add(level, internal, tone); }

    FloatParam level {"Level", 0.f, 1.f, 0.5f, {.versionHint = 1}};
    FloatParam internal {
        "Internal", 0.f, 1.f, 0.f, {.automatable = false, .versionHint = 1}};
    FloatParam tone {"Tone", 0.f, 1.f, 0.5f, {.versionHint = 1}};
};

// SVFFilter's next release as a port: a Drive inserted mid-tree, in the BP
// band, with that release's hint, for IdentityOverrides::indexedThroughVersion 1.
struct DrivenSVFBand : ParameterGroup
{
    DrivenSVFBand()
        : ParameterGroup("BP")
    {
        add(enabled, drive, cutoff, resonance);
    }

    BoolParam enabled {"Enable", false, {.versionHint = 1}};
    FloatParam drive {"Drive", 0.f, 1.f, 0.f, {.versionHint = 2}};
    FloatParam cutoff {"Cutoff", 0.f, 1.f, 0.5663233399391174f, {.versionHint = 1}};
    FloatParam resonance {"Resonance", 0.f, 1.f, 0.2f, {.versionHint = 1}};
};

struct NewerSVFParams : ParameterGroup
{
    NewerSVFParams() { add(highPass, bandPass, lowPass); }

    SVFBand highPass {"HP", false, 0.23298999667167664f};
    DrivenSVFBand bandPass;
    SVFBand lowPass {"LP", true, 0.7253637313842773f};
};

using SVFPort = PlugPort<SVFParams>;
using ChorusPort = PlugPort<ChorusParams>;
using SparsePort = PlugPort<SparseParams>;
using NewerSVFPort = PlugPort<NewerSVFParams>;

// A port with no loadLegacy at all, which IdScheme::TamberPlug alone lets read
// Plug's AU sessions.
template <typename ParamsT>
struct PlainPlugPort : StatePlugin<State<ParamsT>>
{
    std::string_view name() const override { return "PlainPlugPort"; }
    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}
};

using PlainSVFPort = PlainPlugPort<SVFParams>;
using PlainChorusPort = PlainPlugPort<ChorusParams>;

// The ports under IdScheme::TamberPlug, for the AU suites' port factory and the
// AUv3 suite, under codes of their own: SVFFilter 'Psvf', Chorus 'Pchr', Sparse
// 'Pspr', SVFFilter's next release 'Psv2' (indexedThroughVersion 1), and the
// tests' gain 'Pgan', for its skewed (curve Other) cutoff; SVFFilter and Chorus
// with no loadLegacy, 'Pnsv' and 'Pnch'.
template <typename P>
PluginDescription plugDescription(std::string_view name,
                                  FourCC code,
                                  std::optional<int> indexedThrough = {})
{
    return {.name = name,
            .pluginCode = code,
            .create = []
            { return TestPlugins::Module::create<P>(PluginCreateFn {}); },
            .idScheme = IdScheme::TamberPlug,
            .overrides = {.indexedThroughVersion = indexedThrough}};
}

inline const Vector<PluginDescription>& plugDescriptions()
{
    static const auto all = []
    {
        auto result = Vector<PluginDescription> {};
        result.add(plugDescription<SVFPort>("Plug SVFFilter", "Psvf"));
        result.add(plugDescription<ChorusPort>("Plug Chorus", "Pchr"));
        result.add(plugDescription<SparsePort>("Plug Sparse", "Pspr"));
        result.add(plugDescription<NewerSVFPort>("Plug SVFFilter 2", "Psv2", 1));
        result.add(plugDescription<TestPlugins::GainPlugin>("Plug Gain", "Pgan"));
        result.add(plugDescription<PlainSVFPort>("Plain SVFFilter", "Pnsv"));
        result.add(plugDescription<PlainChorusPort>("Plain Chorus", "Pnch"));
        return result;
    }();

    return all;
}

// Null for any other subtype.
inline const PluginDescription* plugDescriptionFor(uint32_t subtype)
{
    for (const auto& description: plugDescriptions())
        if (description.pluginCode.toUint32() == subtype)
            return &description;

    return nullptr;
}

} // namespace TamberPlugFixtures
