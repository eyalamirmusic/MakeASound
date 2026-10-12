#include "HostParameters.h"

#include <algorithm>
#include <cmath>

namespace MakeASound::AU
{

CFStringRef makeCFString(std::string_view text)
{
    return CFStringCreateWithBytes(kCFAllocatorDefault,
                                   reinterpret_cast<const UInt8*>(text.data()),
                                   static_cast<CFIndex>(text.size()),
                                   kCFStringEncodingUTF8,
                                   false);
}

std::string toUtf8(CFStringRef text)
{
    if (text == nullptr)
        return {};

    auto range = CFRangeMake(0, CFStringGetLength(text));
    auto size = CFIndex {};
    CFStringGetBytes(
        text, range, kCFStringEncodingUTF8, '?', false, nullptr, 0, &size);

    auto result = std::string(static_cast<size_t>(size), '\0');
    CFStringGetBytes(text,
                     range,
                     kCFStringEncodingUTF8,
                     '?',
                     false,
                     reinterpret_cast<UInt8*>(result.data()),
                     size,
                     nullptr);
    return result;
}

void fillParameterInfo(const ParameterList::Entry& entry,
                       AudioUnitParameterInfo& info)
{
    const auto& param = *entry.param;

    info = AudioUnitParameterInfo {};
    info.cfNameString = makeCFString(entry.displayName);
    CFStringGetCString(
        info.cfNameString, info.name, sizeof(info.name), kCFStringEncodingUTF8);

    info.minValue = param.minValue();
    info.maxValue = param.maxValue();
    info.defaultValue = param.defaultValue();

    if (param.stateFormat() == Parameter::StateFormat::Bool)
        info.unit = kAudioUnitParameterUnit_Boolean;
    else if (param.numSteps() > 0)
        info.unit = kAudioUnitParameterUnit_Indexed;
    else
        info.unit = kAudioUnitParameterUnit_Generic;

    info.flags = kAudioUnitParameterFlag_IsReadable
                 | kAudioUnitParameterFlag_IsWritable
                 | kAudioUnitParameterFlag_HasCFNameString
                 | kAudioUnitParameterFlag_CFNameRelease;

    if (hasValueStrings(param))
        info.flags |= kAudioUnitParameterFlag_ValuesHaveStrings;
}

bool hasValueStrings(const Parameter& param) noexcept
{
    auto format = param.stateFormat();
    return param.numSteps() > 0
           && (format == Parameter::StateFormat::Text
               || format == Parameter::StateFormat::Bool);
}

CFArrayRef copyValueStrings(const Parameter& param)
{
    if (!hasValueStrings(param))
        return nullptr;

    auto steps = param.numSteps();
    auto* array =
        CFArrayCreateMutable(kCFAllocatorDefault, steps + 1, &kCFTypeArrayCallBacks);

    for (auto step = 0; step <= steps; ++step)
    {
        auto plain =
            param.toPlain(static_cast<float>(step) / static_cast<float>(steps));
        auto* text = copyTextForValue(param, plain);
        CFArrayAppendValue(array, text);
        CFRelease(text);
    }

    return array;
}

CFStringRef copyTextForValue(const Parameter& param, float plain)
{
    return makeCFString(param.valueToText(plain));
}

float valueForText(const Parameter& param, CFStringRef text)
{
    auto value = param.textToValue(toUtf8(text));

    if (!std::isfinite(value))
        return param.getValue();

    return std::clamp(value, param.minValue(), param.maxValue());
}

} // namespace MakeASound::AU
