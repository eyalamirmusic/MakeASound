#pragma once

#include <Miro/Miro.h>
#include <magic_enum/magic_enum.hpp>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include "../DeviceInfo.h"

namespace Miro
{
template <typename T>
    requires std::is_integral_v<T>
             && (!std::is_same_v<T, bool>) && (!std::is_same_v<T, int>)
void reflect(Reflector& ref, T& value)
{
    if (ref.isSaving())
        ref.json = JSON(static_cast<double>(value));
    else if (ref.json.isNumber())
        value = static_cast<T>(ref.json.asNumber());
}

template <typename E>
    requires std::is_enum_v<E>
void reflect(Reflector& ref, E& value)
{
    if (ref.isSaving())
    {
        ref.json = JSON(std::string(magic_enum::enum_name(value)));
    }
    else if (ref.json.isString())
    {
        if (auto parsed = magic_enum::enum_cast<E>(ref.json.asString()))
            value = *parsed;
    }
}

template <typename T>
void reflect(Reflector& ref, std::optional<T>& value)
{
    if (ref.isSaving())
    {
        if (value)
            reflect(ref, *value);
        else
            ref.json = JSON(nullptr);
    }
    else if (ref.json.isNull())
    {
        value.reset();
    }
    else
    {
        auto inner = T {};
        reflect(ref, inner);
        value = std::move(inner);
    }
}
} // namespace Miro

namespace MakeASound
{
using JSON = Miro::JSON;

inline void reflect(Miro::Reflector& ref, DeviceInfo& v)
{
    ref["id"](v.id);
    ref["name"](v.name);
    ref["outputChannels"](v.outputChannels);
    ref["inputChannels"](v.inputChannels);
    ref["duplexChannels"](v.duplexChannels);
    ref["isDefaultOutput"](v.isDefaultOutput);
    ref["isDefaultInput"](v.isDefaultInput);
    ref["sampleRates"](v.sampleRates);
    ref["currentSampleRate"](v.currentSampleRate);
    ref["preferredSampleRate"](v.preferredSampleRate);
    ref["nativeFormats"](v.nativeFormats);
}

inline void reflect(Miro::Reflector& ref, StreamParameters& v)
{
    ref["device"](v.device);
    ref["nChannels"](v.nChannels);
    ref["firstChannel"](v.firstChannel);
}

inline void reflect(Miro::Reflector& ref, Flags& v)
{
    ref["nonInterleaved"](v.nonInterleaved);
    ref["minimizeLatency"](v.minimizeLatency);
    ref["hogDevice"](v.hogDevice);
    ref["scheduleRealTime"](v.scheduleRealTime);
    ref["alsaUseDefault"](v.alsaUseDefault);
    ref["jackDontConnect"](v.jackDontConnect);
}

inline void reflect(Miro::Reflector& ref, StreamOptions& v)
{
    ref["flags"](v.flags);
    ref["numberOfBuffers"](v.numberOfBuffers);
    ref["streamName"](v.streamName);
    ref["priority"](v.priority);
}

inline void reflect(Miro::Reflector& ref, StreamConfig& v)
{
    ref["input"](v.input);
    ref["output"](v.output);
    ref["format"](v.format);
    ref["sampleRate"](v.sampleRate);
    ref["maxBlockSize"](v.maxBlockSize);
    ref["options"](v.options);
}

template <typename T>
JSON getJSON(const T& object)
{
    return Miro::toJSON(object);
}

template <typename T>
std::string getJSONString(const T& object, int indent = 4)
{
    return Miro::toJSONString(object, indent);
}

template <typename T>
void print(const T& object)
{
    std::cout << getJSONString(object) << std::endl;
}

template <typename T>
T fromJSON(const JSON& json)
{
    return Miro::createFromJSON<T>(json);
}

template <typename T>
T fromJSONString(const std::string& text)
{
    return Miro::createFromJSONString<T>(text);
}

} // namespace MakeASound
