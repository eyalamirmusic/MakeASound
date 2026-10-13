#pragma once

#include "../Core/Plugin.h"
#include "../../Common/Common.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Readers for the bytes a JUCE build of a plugin saved, so the plugin's
// loadForeignState can take its users' old sessions. Pure and SDK-free; each
// returns nullopt or an empty result rather than throwing. Byte formats are
// JUCE 8.0.12's (paths relative to JUCE's modules/).
namespace MakeASound::Compat
{

// String::hashCode and hashCode64 (juce_core/text/juce_String.cpp): h = 31h + c
// in 32 bits and h = 101h + c in 64, from 0, over the code points of the UTF-8
// text as CharPointer_UTF8 decodes them, a NUL ending it. A JUCE VST3 or AU
// parameter id is juceStringHash(id) & 0x7fffffff, an AUv3 address
// juceStringHash64(id).
uint32_t juceStringHash(std::string_view utf8) noexcept;
uint64_t juceStringHash64(std::string_view utf8) noexcept;

// AudioProcessor::copyXmlToBinary
// (juce_audio_processors_headless/processors/juce_AudioProcessor.cpp): "VC2!"
// (0x21324356 little-endian), a little-endian u32 length, that much single-line
// UTF-8 XML and a NUL. True when the bytes start so and are longer than 8.
bool isJuceXmlBlob(std::string_view bytes) noexcept;

// The XML text of such a blob, clamped to the bytes there are and cut at its
// first NUL, as getXmlFromBinary reads it; nullopt when it is no blob or the
// length is 0.
std::optional<std::string_view> juceXmlText(std::string_view bytes) noexcept;

// What the JUCE VST3 wrapper appends to the plugin's bytes
// (juce_audio_plugin_client_VST3.cpp, getStateInformation): an i64 0, the
// private ValueTree `JUCEPrivateData {Bypass}` (only when the plugin had no
// bypass parameter of its own), an i64 holding that tree's size and the 15
// bytes "JUCEPrivateData", all little-endian.
struct JucePrivateData
{
    // The bytes before the trailer: the plugin's own. All of them when there
    // was no trailer, or one whose sizes do not fit the bytes.
    std::string_view pluginState;

    // The wrapper's bypass, present when the trailer carried a private tree:
    // its `Bypass`, false when the tree is unreadable or has none, as JUCE
    // reads it.
    std::optional<bool> bypass;
};

JucePrivateData stripJucePrivateData(std::string_view state);

// A VST2 program inside an 'FxBk' bank: 'CcnK', size, 'FxCk' (values) or 'FPCh'
// (a chunk), version, id, version, the value count and a 28-byte name, all
// big-endian.
struct Vst2Program
{
    std::string name;
    Vector<float> values;
    std::string_view chunk;
};

// The VST2 state a VST3 that replaces a VST2 saves and reads
// (VST3_SDK/public.sdk/source/vst/utility/vst2persistence.cpp, big-endian
// throughout): an optional 'VstW' header (size, version, bypass), then 'CcnK',
// size, 'FBCh' with one chunk or 'FxBk' with programs, version, the VST2 unique
// id, the plugin version, the program count, from version 1 the current program
// and 124 reserved bytes, then the chunk's size and bytes or the programs.
struct Vst2Bank
{
    int32_t fxUniqueId = 0;
    int32_t fxVersion = 0;
    int32_t currentProgram = 0;

    // From the 'VstW' header, when there was one.
    std::optional<bool> bypass;

    // The 'FBCh' chunk, a view into the bytes read; empty for 'FxBk'.
    std::string_view chunk;

    // The 'FxBk' programs; empty for 'FBCh'.
    Vector<Vst2Program> programs;
};

// Nullopt for anything tryVst2StateLoad refuses: no 'CcnK', another magic, an
// 'FxBk' without programs, an empty chunk, or bytes that end early.
std::optional<Vst2Bank> readVst2Bank(std::string_view bytes);

// A JUCE VST3's whole component state, as its setState took it apart: a VST2
// bank's chunk when the bytes are one (origin JuceVST2Bank, `program` the
// bank's current program; for an 'FxBk' bank `bytes` stays the whole bank),
// otherwise the bytes themselves (origin JuceVST3), then the private-data
// trailer stripped. `bypass` is the trailer's, else the 'VstW' header's. A view
// into `state`, which must outlive it.
ForeignState juceVST3State(std::string_view state,
                           StateContext context = StateContext::Session);

// MemoryBlock::fromBase64Encoding (juce_core/memory/juce_MemoryBlock.cpp), which
// is not RFC 4648: "<byte count>.<chars>", the alphabet
// ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+", each
// character six bits filled from the least significant end, any other character
// skipped and bits past the count dropped. Nullopt without the '.' or with a
// negative count.
std::optional<std::string> fromJuceBase64(std::string_view text);

// The value of one entry of a JUCE PropertiesFile in its XML form, `<PROPERTIES>`
// of `<VALUE name= val=>` (juce_data_structures/app_properties); nullopt when the
// file is not one or has no such entry.
std::optional<std::string> juceSettingsValue(std::string_view propertiesXml,
                                             std::string_view name);

// A JUCE standalone's plugin state out of its settings file
// (`~/Library/Application Support/<Name>.settings` on macOS,
// `%APPDATA%\<Name>\<Name>.settings` on Windows, `~/.config/<Name>.settings` on
// Linux, `<Name>` being JucePlugin_Name): its `filterState`, base64 decoded.
// Nullopt when there is none or it decodes to nothing.
std::optional<std::string> juceStandaloneState(std::string_view propertiesXml);

} // namespace MakeASound::Compat
