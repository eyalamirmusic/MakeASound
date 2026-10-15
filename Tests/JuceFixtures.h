#pragma once

// Bytes JUCE 8.0.12 wrote, shared by the JuceCompat/ readers' cases and every
// format suite that hands its adapter a JUCE build's saved state. They were
// dumped by a throwaway JUCE console program: an AudioProcessorValueTreeState of
// root type "FixturePlugin" with a skewed float `cutoff` (20..20000, skew 0.3) at
// 440, a choice `mode` at index 2, a bool `enabled` off, an int `voices` (1..16)
// at 5, `attack` (0..5) inside a group at 0.25, and a float whose id `tone&é"<>`
// needs escaping at 0.75, plus an int property `version` 3 on the root; its
// copyXmlToBinary blob, that blob's MemoryBlock::toBase64Encoding, the tree's
// ValueTree::writeToStream, the VST3 wrapper's trailer as its
// getStateInformation appends it (the same JUCE calls, verbatim), and
// String::hashCode/hashCode64 of a few ids. The VST2 banks are built here from
// vst2persistence.cpp's writer, which needs the VST3 SDK's IBStream to run.
//
// FixturePlugin is the port of that JUCE plugin a format suite hosts: the same
// parameters, its loadLegacy reading the blob through Compat::loadJuceApvtsBlob.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace JuceFixtures
{
using namespace MakeASound;

// clang-format off: the fixtures, fourteen bytes a row.

// copyXmlToBinary of the fixture state: "VC2!", length 312, the XML, a NUL.
inline constexpr unsigned char apvtsBlob[] = {
    0x56, 0x43, 0x32, 0x21, 0x38, 0x01, 0x00, 0x00, 0x3c, 0x3f, 0x78, 0x6d, 0x6c, 0x20,
    0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x3d, 0x22, 0x31, 0x2e, 0x30, 0x22, 0x20,
    0x65, 0x6e, 0x63, 0x6f, 0x64, 0x69, 0x6e, 0x67, 0x3d, 0x22, 0x55, 0x54, 0x46, 0x2d,
    0x38, 0x22, 0x3f, 0x3e, 0x20, 0x3c, 0x46, 0x69, 0x78, 0x74, 0x75, 0x72, 0x65, 0x50,
    0x6c, 0x75, 0x67, 0x69, 0x6e, 0x20, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x3d,
    0x22, 0x33, 0x22, 0x3e, 0x3c, 0x50, 0x41, 0x52, 0x41, 0x4d, 0x20, 0x69, 0x64, 0x3d,
    0x22, 0x61, 0x74, 0x74, 0x61, 0x63, 0x6b, 0x22, 0x20, 0x76, 0x61, 0x6c, 0x75, 0x65,
    0x3d, 0x22, 0x30, 0x2e, 0x32, 0x35, 0x22, 0x2f, 0x3e, 0x3c, 0x50, 0x41, 0x52, 0x41,
    0x4d, 0x20, 0x69, 0x64, 0x3d, 0x22, 0x63, 0x75, 0x74, 0x6f, 0x66, 0x66, 0x22, 0x20,
    0x76, 0x61, 0x6c, 0x75, 0x65, 0x3d, 0x22, 0x34, 0x34, 0x30, 0x2e, 0x30, 0x30, 0x30,
    0x30, 0x36, 0x31, 0x30, 0x33, 0x35, 0x31, 0x35, 0x36, 0x32, 0x22, 0x2f, 0x3e, 0x3c,
    0x50, 0x41, 0x52, 0x41, 0x4d, 0x20, 0x69, 0x64, 0x3d, 0x22, 0x65, 0x6e, 0x61, 0x62,
    0x6c, 0x65, 0x64, 0x22, 0x20, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x3d, 0x22, 0x30, 0x2e,
    0x30, 0x22, 0x2f, 0x3e, 0x3c, 0x50, 0x41, 0x52, 0x41, 0x4d, 0x20, 0x69, 0x64, 0x3d,
    0x22, 0x6d, 0x6f, 0x64, 0x65, 0x22, 0x20, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x3d, 0x22,
    0x32, 0x2e, 0x30, 0x22, 0x2f, 0x3e, 0x3c, 0x50, 0x41, 0x52, 0x41, 0x4d, 0x20, 0x69,
    0x64, 0x3d, 0x22, 0x74, 0x6f, 0x6e, 0x65, 0x26, 0x61, 0x6d, 0x70, 0x3b, 0x26, 0x23,
    0x32, 0x33, 0x33, 0x3b, 0x26, 0x71, 0x75, 0x6f, 0x74, 0x3b, 0x26, 0x6c, 0x74, 0x3b,
    0x26, 0x67, 0x74, 0x3b, 0x22, 0x20, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x3d, 0x22, 0x30,
    0x2e, 0x37, 0x35, 0x22, 0x2f, 0x3e, 0x3c, 0x50, 0x41, 0x52, 0x41, 0x4d, 0x20, 0x69,
    0x64, 0x3d, 0x22, 0x76, 0x6f, 0x69, 0x63, 0x65, 0x73, 0x22, 0x20, 0x76, 0x61, 0x6c,
    0x75, 0x65, 0x3d, 0x22, 0x35, 0x2e, 0x30, 0x22, 0x2f, 0x3e, 0x3c, 0x2f, 0x46, 0x69,
    0x78, 0x74, 0x75, 0x72, 0x65, 0x50, 0x6c, 0x75, 0x67, 0x69, 0x6e, 0x3e, 0x00};

// The same blob's MemoryBlock::toBase64Encoding.
inline constexpr auto apvtsBase64 = std::string_view {
    "321.VMjLgfS....O+fWarAhckI2bo8la8HRLt.iHfTlai8FYo41Y8HRUTYTK3HxO9.BOFkFdzUmbkAEa0cVZ"
    "tAhckI2bo8la8HxLh3COPEjTA0DHoQVOhDFczE1XqIBH1EFa0UVOh.iKxTiHu3COPEjTA0DHoQVOhLVcz8lY"
    "lIBH1EFa0UVOhPCMv3BLv.CL1DCLyTSL0XiLh7hO7.UPRETSfjFY8HRYtElXrUFYh.hcgwVck0iHv3BLh7hO"
    "7.UPRETSfjFY8HRauQVYh.hcgwVck0iHx3BLh7hO7.UPRETSfjFY8HBcu4VYlDVavsiIiHyLyriIwU2azsiI"
    "rQ2NlbFc6HBH1EFa0UVOh.iK2TiHu3COPEjTA0DHoQVOhX2aoMVYyIBH1EFa0UVOhTiKvHxK9vyKFkFdzUmb"
    "kAEa0cVZt4C."};

// The fixture state's ValueTree::writeToStream.
inline constexpr unsigned char apvtsValueTree[] = {
    0x46, 0x69, 0x78, 0x74, 0x75, 0x72, 0x65, 0x50, 0x6c, 0x75, 0x67, 0x69, 0x6e, 0x00,
    0x01, 0x01, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x00, 0x01, 0x05, 0x01, 0x03,
    0x00, 0x00, 0x00, 0x01, 0x06, 0x50, 0x41, 0x52, 0x41, 0x4d, 0x00, 0x01, 0x02, 0x69,
    0x64, 0x00, 0x01, 0x08, 0x05, 0x61, 0x74, 0x74, 0x61, 0x63, 0x6b, 0x00, 0x76, 0x61,
    0x6c, 0x75, 0x65, 0x00, 0x01, 0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd0,
    0x3f, 0x00, 0x50, 0x41, 0x52, 0x41, 0x4d, 0x00, 0x01, 0x02, 0x69, 0x64, 0x00, 0x01,
    0x08, 0x05, 0x63, 0x75, 0x74, 0x6f, 0x66, 0x66, 0x00, 0x76, 0x61, 0x6c, 0x75, 0x65,
    0x00, 0x01, 0x09, 0x04, 0x00, 0x00, 0x00, 0x40, 0x00, 0x80, 0x7b, 0x40, 0x00, 0x50,
    0x41, 0x52, 0x41, 0x4d, 0x00, 0x01, 0x02, 0x69, 0x64, 0x00, 0x01, 0x09, 0x05, 0x65,
    0x6e, 0x61, 0x62, 0x6c, 0x65, 0x64, 0x00, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x00, 0x01,
    0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x41, 0x52,
    0x41, 0x4d, 0x00, 0x01, 0x02, 0x69, 0x64, 0x00, 0x01, 0x06, 0x05, 0x6d, 0x6f, 0x64,
    0x65, 0x00, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x00, 0x01, 0x09, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x50, 0x41, 0x52, 0x41, 0x4d, 0x00, 0x01, 0x02,
    0x69, 0x64, 0x00, 0x01, 0x0c, 0x05, 0x74, 0x6f, 0x6e, 0x65, 0x26, 0xc3, 0xa9, 0x22,
    0x3c, 0x3e, 0x00, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x00, 0x01, 0x09, 0x04, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xe8, 0x3f, 0x00, 0x50, 0x41, 0x52, 0x41, 0x4d, 0x00, 0x01,
    0x02, 0x69, 0x64, 0x00, 0x01, 0x08, 0x05, 0x76, 0x6f, 0x69, 0x63, 0x65, 0x73, 0x00,
    0x76, 0x61, 0x6c, 0x75, 0x65, 0x00, 0x01, 0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x14, 0x40, 0x00};

// What the VST3 wrapper appends for a plugin with no bypass parameter, bypassed:
// i64 0, `JUCEPrivateData {Bypass: true}`, its size 29, the magic.
inline constexpr unsigned char bypassedTrailer[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x55, 0x43, 0x45, 0x50, 0x72,
    0x69, 0x76, 0x61, 0x74, 0x65, 0x44, 0x61, 0x74, 0x61, 0x00, 0x01, 0x01, 0x42, 0x79,
    0x70, 0x61, 0x73, 0x73, 0x00, 0x01, 0x01, 0x02, 0x00, 0x1d, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x4a, 0x55, 0x43, 0x45, 0x50, 0x72, 0x69, 0x76, 0x61, 0x74, 0x65,
    0x44, 0x61, 0x74, 0x61};

// And for a plugin with its own bypass: no private tree, size 0.
inline constexpr unsigned char emptyTrailer[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x55, 0x43, 0x45, 0x50, 0x72,
    0x69, 0x76, 0x61, 0x74, 0x65, 0x44, 0x61, 0x74, 0x61};

// clang-format on

template <size_t N>
std::string_view bytesOf(const unsigned char (&bytes)[N])
{
    return {reinterpret_cast<const char*>(bytes), N};
}

inline std::string blob()
{
    return std::string {bytesOf(apvtsBlob)};
}

inline std::string withTrailer(const std::string& state, std::string_view trailer)
{
    return state + std::string {trailer};
}

// vst2persistence.cpp's writer, big-endian.
struct BigEndianWriter
{
    void i32(int32_t value)
    {
        for (auto shift = 24; shift >= 0; shift -= 8)
            bytes +=
                static_cast<char>((static_cast<uint32_t>(value) >> shift) & 0xff);
    }

    void fourCC(const char (&code)[5]) { bytes.append(code, 4); }

    void f32(float value)
    {
        auto bits = uint32_t {0};
        std::memcpy(&bits, &value, sizeof bits);
        i32(static_cast<int32_t>(bits));
    }

    void zeros(size_t count) { bytes.append(count, '\0'); }

    // Back-fills the i32 size at `at` with what follows it.
    void patchSize(size_t at)
    {
        auto size = static_cast<uint32_t>(bytes.size() - at - 4);

        for (auto i = 0; i < 4; ++i)
            bytes[at + static_cast<size_t>(i)] =
                static_cast<char>((size >> (24 - 8 * i)) & 0xff);
    }

    std::string bytes;
};

inline void writeBypassHeader(BigEndianWriter& out, bool bypassed)
{
    out.fourCC("VstW");
    out.i32(8);
    out.i32(1);
    out.i32(bypassed ? 1 : 0);
}

// writeVst2State with a chunk: version 2, so a current program and 124 zeros.
inline std::string chunkBank(std::string_view chunk,
                             std::optional<bool> bypass,
                             int currentProgram = 3,
                             int version = 2)
{
    auto out = BigEndianWriter {};

    if (bypass)
        writeBypassHeader(out, *bypass);

    out.fourCC("CcnK");
    auto size = out.bytes.size();
    out.i32(0);
    out.fourCC("FBCh");
    out.i32(version);
    out.fourCC("Fxtr");
    out.i32(0x10203);
    out.i32(1);

    if (version >= 1)
    {
        out.i32(currentProgram);
        out.zeros(124);
    }

    out.i32(static_cast<int32_t>(chunk.size()));
    out.bytes.append(chunk);
    out.patchSize(size);
    return out.bytes;
}

inline std::string valuesBank()
{
    auto out = BigEndianWriter {};
    out.fourCC("CcnK");
    auto size = out.bytes.size();
    out.i32(0);
    out.fourCC("FxBk");
    out.i32(2);
    out.fourCC("Fxtr");
    out.i32(1);
    out.i32(2);
    out.i32(1);
    out.zeros(124);

    for (auto program = 0; program < 2; ++program)
    {
        out.fourCC("CcnK");
        auto programSize = out.bytes.size();
        out.i32(0);
        out.fourCC("FxCk");
        out.i32(1);
        out.fourCC("Fxtr");
        out.i32(1);
        out.i32(3);

        auto name = std::string(program == 0 ? "Init" : "Bright");
        name.resize(28);
        out.bytes += name;

        for (auto value = 0; value < 3; ++value)
            out.f32(static_cast<float>(program) + 0.25f * static_cast<float>(value));

        out.patchSize(programSize);
    }

    out.patchSize(size);
    return out.bytes;
}

// A JUCE standalone's settings file as PropertiesFile::saveAsXml writes it, the
// audio setup as a child and `filterState` the state's JUCE base64.
inline std::string standaloneSettings(std::string_view filterState)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n\r\n<PROPERTIES>\r\n"
           "  <VALUE name=\"audioSetup\">\r\n"
           "    <DEVICESETUP deviceType=\"CoreAudio\"/>\r\n  </VALUE>\r\n"
           "  <VALUE name=\"filterState\" val=\""
           + std::string {filterState}
           + "\"/>\r\n  <VALUE name=\"shouldMuteInput\" val=\"1\"/>\r\n"
             "</PROPERTIES>\r\n";
}

inline bool fixtureNear(float a, float b, float tolerance = 1e-4f)
{
    return std::abs(a - b) < tolerance;
}

struct EnvelopeParams : ParameterGroup
{
    EnvelopeParams()
        : ParameterGroup("Envelope", {.id = "env"})
    {
        add(attack);
    }

    FloatParam attack {
        "Attack", 0.f, 5.f, 0.01f, {.id = "attack", .versionHint = 1}};
};

// The JUCE fixture's cutoff range, NormalisableRange<float> (20, 20000, 0, 0.3):
// what a host lane's normalized value means for it.
inline constexpr auto cutoffRange = FloatRange {20.f, 20000.f, 0.f, 0.3f};

// The fixture's parameters as a port would declare them, each with the
// versionHint an AU needs.
struct FixtureParams : ParameterGroup
{
    FixtureParams() { add(cutoff, mode, enabled, voices, env, tone, extra); }

    FloatParam cutoff {
        "Cutoff", cutoffRange, 1000.f, {.id = "cutoff", .versionHint = 1}};
    ChoiceParam mode {
        "Mode", {"Clean", "Warm", "Hot"}, 0, {.id = "mode", .versionHint = 1}};
    BoolParam enabled {"Enabled", true, {.id = "enabled", .versionHint = 1}};
    IntParam voices {"Voices", 1, 16, 8, {.id = "voices", .versionHint = 1}};
    EnvelopeParams env;
    FloatParam tone {
        "Tone", 0.f, 1.f, 0.5f, {.id = "tone&\xc3\xa9\"<>", .versionHint = 1}};

    // Never in the JUCE state.
    FloatParam extra {"Extra", 0.f, 1.f, 0.3f, {.id = "extra", .versionHint = 1}};
};

inline bool holdsFixtureValues(const FixtureParams& params)
{
    return fixtureNear(params.cutoff.get(), 440.f) && params.mode.getIndex() == 2
           && !params.enabled.isOn() && params.voices.get() == 5
           && fixtureNear(params.env.attack.get(), 0.25f)
           && fixtureNear(params.tone.get(), 0.75f);
}

// True when no parameter has left its default.
inline bool holdsDefaults(const FixtureParams& params)
{
    return fixtureNear(params.cutoff.get(), 1000.f) && params.mode.getIndex() == 0
           && params.enabled.isOn() && params.voices.get() == 8
           && fixtureNear(params.env.attack.get(), 0.01f)
           && fixtureNear(params.tone.get(), 0.5f);
}

// The JUCE plugin above ported: an effect with no bypass parameter of its own,
// so the wrapper adds one, reading what its JUCE build saved through
// loadJuceApvtsBlob. It records what each foreign load handed it (`bytes` the
// plugin's own, as the format's reader took them apart), and refuses
// every one while `refuse` is set.
struct FixturePlugin : StatePlugin<State<FixtureParams>>
{
    std::string_view name() const override { return "Fixture"; }
    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    bool loadLegacy(const ForeignState& foreign) override
    {
        ++legacyLoads;
        origin = foreign.origin;
        context = foreign.context;
        bypass = foreign.bypass;
        program = foreign.program;
        bytes = std::string {foreign.bytes};

        if (refuse)
            return false;

        return Compat::loadJuceApvtsBlob(
                   foreign.bytes, params, {.rootType = "FixturePlugin"})
            .has_value();
    }

    std::atomic<int> legacyLoads {0};
    std::atomic<bool> refuse {false};
    ForeignOrigin origin = ForeignOrigin::JuceVST3;
    StateContext context = StateContext::Preset;
    std::optional<bool> bypass;
    std::optional<int> program;
    std::string bytes;
};

// The parameter the wrapper added to a FixturePlugin, on (1) or off (0).
inline float addedBypassOf(Plugin& plugin)
{
    const auto& list = plugin.parameters();
    auto index = list.bypassIndex();
    return index < 0 ? -1.f : list[index].getValue();
}

} // namespace JuceFixtures
