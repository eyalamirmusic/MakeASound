#include "State.h"

namespace MakeASound::AU
{

namespace
{
const auto stateKey = CFSTR("MakeASoundState");

// kAUPresetVersionKey, which AudioUnitUtilities.h spells without CFSTR.
const auto versionKey = CFSTR("version");

bool isDictionary(CFPropertyListRef list) noexcept
{
    return list != nullptr && CFGetTypeID(list) == CFDictionaryGetTypeID();
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

} // namespace MakeASound::AU
