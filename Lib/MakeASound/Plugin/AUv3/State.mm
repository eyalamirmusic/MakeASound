#include "State.h"

namespace MakeASound::AUv3
{

namespace
{
NSString* const stateKey = @"MakeASoundState";

// JUCE_STATE_DICTIONARY_KEY, which a JUCE build's AUv3 and v2 both write.
NSString* const juceStateKey = @"jucePluginState";

// TAMBERPLUG_AU_STATE_KEY, which a Tamber Plug build's AU v2 writes.
NSString* const tamberPlugStateKey = @"TamberPlugState";

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

// The data under `key`, when the dictionary is this component's.
std::optional<std::string> readData(NSDictionary<NSString*, id>* state,
                                    const AudioComponentDescription& description,
                                    NSString* key)
{
    if (![state isKindOfClass:[NSDictionary class]])
        return std::nullopt;

    if (!agrees(state, @"type", description.componentType)
        || !agrees(state, @"subtype", description.componentSubType)
        || !agrees(state, @"manufacturer", description.componentManufacturer))
        return std::nullopt;

    id value = state[key];

    if (![value isKindOfClass:[NSData class]])
        return std::nullopt;

    auto* data = static_cast<NSData*>(value);
    return std::string(static_cast<const char*>(data.bytes), data.length);
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
    return readData(state, description, stateKey);
}

std::optional<std::string>
    readJuceState(NSDictionary<NSString*, id>* state,
                  const AudioComponentDescription& description)
{
    auto bytes = readData(state, description, juceStateKey);

    if (!bytes || bytes->empty())
        return std::nullopt;

    return bytes;
}

std::optional<std::string>
    readTamberPlugState(NSDictionary<NSString*, id>* state,
                        const AudioComponentDescription& description)
{
    auto bytes = readData(state, description, tamberPlugStateKey);

    if (!bytes || bytes->empty())
        return std::nullopt;

    return bytes;
}

} // namespace MakeASound::AUv3
