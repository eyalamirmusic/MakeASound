#include "State.h"

namespace MakeASound::AUv3
{

namespace
{
NSString* const stateKey = @"MakeASoundState";

// kAUPresetVersionKey's value: the ClassInfo format, which the SDK reads only at 0.
constexpr auto savedStateVersion = 0;

// A key the dictionary lacks says nothing against it; one that disagrees does.
bool agrees(NSDictionary<NSString*, id>* state, NSString* key, OSType expected)
{
    id value = state[key];

    if (value == nil)
        return true;

    return [value isKindOfClass:[NSNumber class]] &&
           [static_cast<NSNumber*>(value) unsignedIntValue] == expected;
}
} // namespace

NSDictionary<NSString*, id>* makeState(const AudioComponentDescription& description,
                                       std::string_view document)
{
    return @{
        @"type" : @(description.componentType),
        @"subtype" : @(description.componentSubType),
        @"manufacturer" : @(description.componentManufacturer),
        @"version" : @(savedStateVersion),
        stateKey : [NSData dataWithBytes:document.data() length:document.size()]
    };
}

std::optional<std::string> readState(NSDictionary<NSString*, id>* state,
                                     const AudioComponentDescription& description)
{
    if (![state isKindOfClass:[NSDictionary class]])
        return std::nullopt;

    if (!agrees(state, @"type", description.componentType)
        || !agrees(state, @"subtype", description.componentSubType)
        || !agrees(state, @"manufacturer", description.componentManufacturer))
        return std::nullopt;

    id value = state[stateKey];

    if (![value isKindOfClass:[NSData class]])
        return std::nullopt;

    auto* data = static_cast<NSData*>(value);
    return std::string(static_cast<const char*>(data.bytes), data.length);
}

} // namespace MakeASound::AUv3
