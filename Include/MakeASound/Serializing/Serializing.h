#pragma once

#include <nlohmann/json.hpp>
#include <magic_enum.hpp>
#include "../DeviceInfo.h"

namespace nlohmann
{
template <typename EnumType>
struct adl_serializer<EnumType, std::enable_if_t<std::is_enum_v<EnumType>>>
{
    static void to_json(json& j, const EnumType& enumVal)
    {
        j = magic_enum::enum_name(enumVal);
    }

    static void from_json(const json& j, EnumType& enumVal)
    {
        if (auto val = magic_enum::enum_cast<EnumType>(j.get<std::string>()))
            enumVal = *val;
    }
};

template <typename T>
struct adl_serializer<std::optional<T>>
{
    static void to_json(json& j, const std::optional<T>& opt)
    {
        if (opt)
            j = *opt;
        else
            j = nullptr;
    }

    static void from_json(const json& j, std::optional<T>& opt)
    {
        if (j.is_null())
            opt.reset(); // Set to nullopt if the JSON is null
        else
            opt = j.get<T>(); // Deserialize the contained value
    }
};
} // namespace nlohmann

namespace MakeASound
{
using json = nlohmann::json;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DeviceInfo,
                                   id,
                                   name,
                                   outputChannels,
                                   inputChannels,
                                   duplexChannels,
                                   isDefaultInput,
                                   isDefaultOutput,
                                   sampleRates,
                                   currentSampleRate,
                                   preferredSampleRate,
                                   nativeFormats);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StreamParameters,
                                   device,
                                   nChannels,
                                   firstChannel);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Flags,
                                   nonInterleaved,
                                   minimizeLatency,
                                   hogDevice,
                                   scheduleRealTime,
                                   alsaUseDefault,
                                   jackDontConnect);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
    StreamOptions, flags, numberOfBuffers, streamName, priority);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
    StreamConfig, input, output, format, sampleRate, maxBlockSize, options);

template <typename T>
json getJSON(const T& object)
{
    return object;
}

template <typename T>
std::string getJSONString(const T& object, int indent = 4)
{
    return getJSON(object).dump(indent);
}

template <typename T>
void print(const T& object)
{
    std::cout << getJSONString(object) << std::endl;
}

template<typename T>
T fromJSON(const json& json)
{
    return json;
}

template<typename T>
T fromJSONString(const std::string& text)
{

}

} // namespace MakeASound
