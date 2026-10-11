#include "Parameter.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace MakeASound
{
namespace
{
float clampUnit(float value) noexcept
{
    return value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
}

char lower(char c) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}
} // namespace

Parameter::Parameter(std::string_view nameToUse, ParameterOptions options)
    : paramName(nameToUse)
    , paramId(options.id.empty() ? nameToUse : options.id)
    , paramShortName(options.shortName)
    , paramLabel(options.label)
    , automatable(options.automatable)
    , bypass(options.bypass)
    , sessionOnly(options.sessionOnly)
    , hostId(options.hostId)
{
}

void Parameter::setNormalized(float normalized) noexcept
{
    setValue(toPlain(normalized));
}

float Parameter::getNormalized() const noexcept
{
    return toNormalized(getValue());
}

float Parameter::toNormalized(float plain) const noexcept
{
    auto low = minValue();
    auto high = maxValue();

    if (high == low)
        return 0.f;

    return clampUnit((plain - low) / (high - low));
}

float Parameter::toPlain(float normalized) const noexcept
{
    auto position = clampUnit(normalized);

    if (auto steps = numSteps(); steps > 0)
        position = std::round(position * static_cast<float>(steps))
                   / static_cast<float>(steps);

    return minValue() + position * (maxValue() - minValue());
}

std::string Parameter::valueToText(float plain) const
{
    auto text = formatNumber("%.2f", plain);

    if (text.find('.') != std::string::npos)
    {
        while (text.back() == '0')
            text.pop_back();

        if (text.back() == '.')
            text.pop_back();
    }

    if (!label().empty())
        text += ' ' + label();

    return text;
}

float Parameter::textToValue(std::string_view text) const
{
    return parseNumber(text).value_or(defaultValue());
}

std::string Parameter::toStateText() const
{
    return shortestText(getValue());
}

bool Parameter::fromStateText(std::string_view text)
{
    auto parsed = parseNumber(text);

    if (!parsed || !std::isfinite(*parsed))
        return false;

    setValue(*parsed);
    return true;
}

std::optional<float> Parameter::parseNumber(std::string_view text)
{
    auto terminated = std::string {trim(text)};
    auto* end = static_cast<char*>(nullptr);

    errno = 0;
    auto value = std::strtof(terminated.c_str(), &end);

    if (end == terminated.c_str() || errno != 0)
        return std::nullopt;

    return value;
}

std::string Parameter::formatNumber(const char* format, float value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, static_cast<double>(value));
    return buffer;
}

std::string Parameter::shortestText(float value)
{
    for (auto digits = 6; digits < 9; ++digits)
    {
        char buffer[32];
        std::snprintf(
            buffer, sizeof buffer, "%.*g", digits, static_cast<double>(value));

        if (std::strtof(buffer, nullptr) == value)
            return buffer;
    }

    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.9g", static_cast<double>(value));
    return buffer;
}

std::string_view Parameter::trim(std::string_view text) noexcept
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);

    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);

    return text;
}

bool Parameter::equalsIgnoringCase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;

    for (auto i = std::size_t {0}; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;

    return true;
}

bool Parameter::containsIgnoringCase(std::string_view text,
                                     std::string_view part) noexcept
{
    if (part.size() > text.size())
        return false;

    for (auto i = std::size_t {0}; i + part.size() <= text.size(); ++i)
        if (equalsIgnoringCase(text.substr(i, part.size()), part))
            return true;

    return false;
}

} // namespace MakeASound
