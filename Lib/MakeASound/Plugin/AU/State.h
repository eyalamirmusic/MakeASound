#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace MakeASound::AU
{

// The plugin's document rides in the SDK's ClassInfo dictionary under one key of
// its own, beside the codes and parameter data the SDK writes and checks.
void writeState(CFPropertyListRef classInfo, std::string_view document);

// Nullopt when the dictionary carries no document of ours.
std::optional<std::string> readState(CFPropertyListRef classInfo);

// Releases a +1 Core Foundation reference.
struct CFReleaser
{
    void operator()(CFTypeRef ref) const noexcept
    {
        if (ref != nullptr)
            CFRelease(ref);
    }
};

using CFOwnedPropertyList =
    std::unique_ptr<std::remove_pointer_t<CFPropertyListRef>, CFReleaser>;

// The SDK restores only a ClassInfo `version` of 0, the saved-state format's,
// but an AUAudioUnit's own fullState writes 1. For a dictionary carrying 1, a
// copy carrying 0; null for anything else, which goes to the SDK as it is.
CFOwnedPropertyList copyWithStateVersionZero(CFPropertyListRef classInfo);

// What a JUCE build of the plugin saved beside the SDK's keys: its
// `jucePluginState` data (AU_1.mm's SaveState), nullopt when the dictionary has
// none or it is empty.
std::optional<std::string> readJuceState(CFPropertyListRef classInfo);

// A copy for the SDK's restore of a JUCE build's ClassInfo, as JUCE's own
// RestoreState makes one: `data` removed, so the JUCE build's parameter ids never
// reach Globals() (which would grow in map mode), and a `version` of 1 made 0.
// Null when the list is no dictionary.
CFOwnedPropertyList copyWithoutParameterData(CFPropertyListRef classInfo);

} // namespace MakeASound::AU
