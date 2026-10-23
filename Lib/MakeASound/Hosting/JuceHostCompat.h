#pragma once

#include "PluginDescription.h"

#include <cstdint>

// What a JUCE 8.0.12 HOST called the plugins it hosted, computed from a
// MakeASound cache row, so a description a JUCE host saved resolves without a
// scan. SDK-free: a caller includes no VST3 header. JUCE paths are relative to
// its modules/.
namespace MakeASound::Compat
{

// PluginDescription::uniqueId as JUCE's host filled it in.
//
// VST3 (juce_audio_processors_headless/format_types/juce_VST3PluginFormatImpl.h,
// getHashForRange(getNormalisedTUID(cid))): h = 31h + w from 0, in 32 bits, over
// the class id's four FUID words (getLong1..4), which are the four 8-digit
// groups of its 32-hex-digit text on every platform.
//
// AU (juce_AudioUnitPluginFormatImpl.h, fillInPluginDescription): type ^ subtype
// ^ manufacturer of the "type:subtype:manufacturer" class id, each a big-endian
// four-char code.
//
// 0 for another format or a class id of the wrong shape.
int32_t juceHostedUniqueId(const Hosting::PluginDescription& description);

// PluginDescription::deprecatedUid. VST3: the same hash over the class id's 16
// raw TUID bytes, each a signed char widened to 32 bits, as JUCE casts them.
// Those bytes are the platform's (VST3::UID::fromString): in text order on
// macOS, the first three groups byte-swapped (COM) on Windows, so the value
// differs between them exactly as JUCE's did. AU: the uniqueId.
int32_t juceHostedDeprecatedUid(const Hosting::PluginDescription& description);

// The order JUCE's host lists a plugin's parameters in (getParameters(), the
// flattened AudioProcessorParameterGroup tree), as positions into the plugin's
// own list. Each listed parameter comes with its group path, outermost first
// (empty: the root): a VST3 parameter's unit and that unit's parents, an AU
// parameter's clump. A group is created where its first parameter (or its
// first sub-group's) is listed and keeps that place among its parent's
// children, so a group's parameters come together there; the rest keep their
// order. With no groups, or groups whose members are contiguous (every JUCE-
// and MakeASound-built plugin), it is the identity.
Vector<int> juceParameterTreeOrder(const Vector<Vector<int64_t>>& groupPaths);

} // namespace MakeASound::Compat
