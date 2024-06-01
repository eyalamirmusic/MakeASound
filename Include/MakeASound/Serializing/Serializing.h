#pragma once

#include <nlohmann/json.hpp>
#include <magic_enum.hpp>

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



} // namespace nlohmann
