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

MIRO_REFLECT_EXTERNAL(MakeASound::DeviceInfo,
                      id,
                      name,
                      outputChannels,
                      inputChannels,
                      duplexChannels,
                      isDefaultOutput,
                      isDefaultInput,
                      sampleRates,
                      currentSampleRate,
                      preferredSampleRate,
                      nativeFormats)

MIRO_REFLECT_EXTERNAL(MakeASound::StreamParameters, device, nChannels, firstChannel)

MIRO_REFLECT_EXTERNAL(MakeASound::Flags,
                      nonInterleaved,
                      minimizeLatency,
                      hogDevice,
                      scheduleRealTime,
                      alsaUseDefault,
                      jackDontConnect)

MIRO_REFLECT_EXTERNAL(MakeASound::StreamOptions,
                      flags,
                      numberOfBuffers,
                      streamName,
                      priority)

MIRO_REFLECT_EXTERNAL(MakeASound::StreamConfig,
                      input,
                      output,
                      format,
                      sampleRate,
                      maxBlockSize,
                      options)

namespace MakeASound
{
using JSON = Miro::JSON;

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
