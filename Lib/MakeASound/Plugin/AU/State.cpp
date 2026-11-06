#include "State.h"

namespace MakeASound::AU
{

namespace
{
const auto stateKey = CFSTR("MakeASoundState");

// kAUPresetVersionKey, which AudioUnitUtilities.h spells without CFSTR.
const auto versionKey = CFSTR("version");

// kAUPresetDataKey: the SDK's parameter values, by element and id.
const auto dataKey = CFSTR("data");

// JUCE_STATE_DICTIONARY_KEY (juce_audio_processors_headless/format_types/
// juce_AU_Shared.h): where a JUCE build keeps its plugin's state.
const auto juceStateKey = CFSTR("jucePluginState");

// TAMBERPLUG_AU_STATE_KEY (Plug's Lib/TamberPlug/AU/Adapter.cpp).
const auto tamberPlugStateKey = CFSTR("TamberPlugState");

bool isDictionary(CFPropertyListRef list) noexcept
{
    return list != nullptr && CFGetTypeID(list) == CFDictionaryGetTypeID();
}

// The non-empty data under `key`.
std::optional<std::string> readForeignData(CFPropertyListRef classInfo,
                                           CFStringRef key)
{
    if (!isDictionary(classInfo))
        return std::nullopt;

    auto value = CFDictionaryGetValue(static_cast<CFDictionaryRef>(classInfo), key);

    if (value == nullptr || CFGetTypeID(value) != CFDataGetTypeID())
        return std::nullopt;

    auto* data = static_cast<CFDataRef>(value);

    if (CFDataGetLength(data) <= 0)
        return std::nullopt;

    return std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                       static_cast<size_t>(CFDataGetLength(data)));
}
} // namespace

void writeState(CFPropertyListRef classInfo, std::string_view document)
{
    if (!isDictionary(classInfo))
        return;

    auto* data = CFDataCreate(kCFAllocatorDefault,
                              reinterpret_cast<const UInt8*>(document.data()),
                              static_cast<CFIndex>(document.size()));

    // The SDK builds ClassInfo as a mutable dictionary and hands it over as const.
    auto* dictionary =
        const_cast<CFMutableDictionaryRef>(static_cast<CFDictionaryRef>(classInfo));
    CFDictionarySetValue(dictionary, stateKey, data);
    CFRelease(data);
}

std::optional<std::string> readState(CFPropertyListRef classInfo)
{
    if (!isDictionary(classInfo))
        return std::nullopt;

    auto value =
        CFDictionaryGetValue(static_cast<CFDictionaryRef>(classInfo), stateKey);

    if (value == nullptr || CFGetTypeID(value) != CFDataGetTypeID())
        return std::nullopt;

    auto* data = static_cast<CFDataRef>(value);
    return std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                       static_cast<size_t>(CFDataGetLength(data)));
}

CFOwnedPropertyList copyWithStateVersionZero(CFPropertyListRef classInfo)
{
    if (!isDictionary(classInfo))
        return {};

    auto* dictionary = static_cast<CFDictionaryRef>(classInfo);
    auto* version = CFDictionaryGetValue(dictionary, versionKey);
    auto number = SInt32 {};

    if (version == nullptr || CFGetTypeID(version) != CFNumberGetTypeID()
        || !CFNumberGetValue(
            static_cast<CFNumberRef>(version), kCFNumberSInt32Type, &number)
        || number != 1)
        return {};

    auto* copy = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, dictionary);
    auto zero = SInt32 {0};
    auto* zeroNumber =
        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &zero);
    CFDictionarySetValue(copy, versionKey, zeroNumber);
    CFRelease(zeroNumber);

    return CFOwnedPropertyList {copy};
}

std::optional<std::string> readJuceState(CFPropertyListRef classInfo)
{
    return readForeignData(classInfo, juceStateKey);
}

std::optional<std::string> readTamberPlugState(CFPropertyListRef classInfo)
{
    return readForeignData(classInfo, tamberPlugStateKey);
}

CFOwnedPropertyList copyWithoutParameterData(CFPropertyListRef classInfo)
{
    if (!isDictionary(classInfo))
        return {};

    auto zeroed = copyWithStateVersionZero(classInfo);
    auto* source = static_cast<CFDictionaryRef>(zeroed ? zeroed.get() : classInfo);
    auto* copy = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, source);
    CFDictionaryRemoveValue(copy, dataKey);

    return CFOwnedPropertyList {copy};
}

} // namespace MakeASound::AU
