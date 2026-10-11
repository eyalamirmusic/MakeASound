#include "State.h"

namespace MakeASound::AU
{

namespace
{
const auto stateKey = CFSTR("MakeASoundState");

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

} // namespace MakeASound::AU
