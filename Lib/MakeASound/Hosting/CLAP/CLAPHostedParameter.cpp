#include "CLAPHostedParameter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace MakeASound::Hosting
{

namespace
{
// The most steps a stepped range is given; past it, it is drawn as continuous.
constexpr auto maxSteps = 1 << 24;

// A name field the plugin may have filled to the brim without a terminator.
template <size_t Size>
std::string parameterName(const char (&field)[Size])
{
    return {field, strnlen(field, Size)};
}

bool has(const clap_param_info& info, uint32_t flag) noexcept
{
    return (info.flags & flag) != 0;
}

double finiteOr(double value, double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

ParameterOptions clapOptionsFor(const clap_param_info& info, const std::string& id)
{
    auto options = ParameterOptions();
    options.id = id;
    options.automatable =
        has(info, CLAP_PARAM_IS_AUTOMATABLE) && !has(info, CLAP_PARAM_IS_READONLY);
    options.bypass = has(info, CLAP_PARAM_IS_BYPASS);
    options.hostId = info.id;
    return options;
}

int stepsOf(const clap_param_info& info, double low, double high) noexcept
{
    if (!has(info, CLAP_PARAM_IS_STEPPED) || !(high > low))
        return 0;

    auto span = std::round(high - low);
    return span >= 1.0 ? static_cast<int>(std::min(span, double {maxSteps})) : 0;
}
} // namespace

CLAPHostedParameter::CLAPHostedParameter(const clap_plugin& pluginToUse,
                                         const clap_plugin_params& paramsToUse,
                                         const clap_param_info& info,
                                         double initialValue,
                                         CLAPParameterSink& sinkToUse)
    : Parameter(parameterName(info.name),
                clapOptionsFor(info, std::to_string(info.id)))
    , plugin(pluginToUse)
    , params(paramsToUse)
    , sink(sinkToUse)
    , clapId(info.id)
    , clapCookie(info.cookie)
    , low(finiteOr(info.min_value, 0.0))
    , high(std::max(low, finiteOr(info.max_value, low)))
    , initial(std::clamp(finiteOr(info.default_value, low), low, high))
    , steps(stepsOf(info, low, high))
    , discrete(steps > 0
               && (has(info, CLAP_PARAM_IS_ENUM) || (low == 0.0 && high == 1.0)))
    , boolean(steps == 1 && low == 0.0 && high == 1.0)
    , readOnly(has(info, CLAP_PARAM_IS_READONLY))
    , value(initial)
{
    value.store(clampPlain(finiteOr(initialValue, initial)),
                std::memory_order_relaxed);
}

double CLAPHostedParameter::clampPlain(double plain) const noexcept
{
    if (!std::isfinite(plain))
        return plainValue();

    auto clamped = std::clamp(plain, low, high);

    if (steps > 0)
        clamped = std::clamp(low + std::round(clamped - low), low, high);

    return clamped;
}

double CLAPHostedParameter::plainFromNormalized(double normalized) const noexcept
{
    if (!std::isfinite(normalized))
        return plainValue();

    return clampPlain(low + std::clamp(normalized, 0.0, 1.0) * (high - low));
}

void CLAPHostedParameter::setValue(float plain) noexcept
{
    if (readOnly || !std::isfinite(plain))
        return;

    auto clamped = clampPlain(static_cast<double>(plain));
    value.store(clamped, std::memory_order_relaxed);
    sink.parameterEdited(*this, clamped);
}

float CLAPHostedParameter::getValue() const noexcept
{
    return static_cast<float>(plainValue());
}

double CLAPHostedParameter::plainValue() const noexcept
{
    return value.load(std::memory_order_relaxed);
}

void CLAPHostedParameter::store(double plain) noexcept
{
    if (std::isfinite(plain))
        value.store(std::clamp(plain, low, high), std::memory_order_relaxed);
}

std::string CLAPHostedParameter::valueToText(float plain) const
{
    auto buffer = std::array<char, 256> {};

    if (params.value_to_text != nullptr
        && params.value_to_text(&plugin,
                                clapId,
                                clampPlain(static_cast<double>(plain)),
                                buffer.data(),
                                static_cast<uint32_t>(buffer.size())))
        return {buffer.data(), strnlen(buffer.data(), buffer.size())};

    return Parameter::valueToText(plain);
}

float CLAPHostedParameter::textToValue(std::string_view input) const
{
    // Terminated for the plugin; whatever the bytes are, they are its to parse.
    const auto terminated = std::string(input);
    auto parsed = 0.0;

    if (params.text_to_value != nullptr
        && params.text_to_value(&plugin, clapId, terminated.c_str(), &parsed)
        && std::isfinite(parsed))
        return static_cast<float>(std::clamp(parsed, low, high));

    return Parameter::textToValue(input);
}

bool CLAPHostedParameter::updateInfo(const clap_param_info& info)
{
    if (info.id != clapId)
        return false;

    auto title = parameterName(info.name);

    if (title == name())
        return false;

    setDisplayInfo(title, shortName(), label());
    return true;
}

} // namespace MakeASound::Hosting
