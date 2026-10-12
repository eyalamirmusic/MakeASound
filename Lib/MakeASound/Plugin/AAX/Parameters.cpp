#include "Parameters.h"

#include <AAX_CLinearTaperDelegate.h>
#include <AAX_CParameter.h>
#include <AAX_CString.h>

#include <algorithm>
#include <cmath>

namespace MakeASound::AAX
{

namespace
{
constexpr char hexDigits[] = "0123456789abcdef";

int hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    return -1;
}
} // namespace

ParamId paramIdFor(uint32_t hostId) noexcept
{
    auto id = ParamId {};

    for (auto digit = 0; digit < 8; ++digit)
        id[static_cast<size_t>(digit)] =
            hexDigits[(hostId >> (28 - 4 * digit)) & 0xf];

    return id;
}

std::optional<uint32_t> hostIdOf(AAX_CParamID id) noexcept
{
    if (id == nullptr)
        return std::nullopt;

    auto value = uint32_t {0};

    for (auto digit = 0; digit < 8; ++digit)
    {
        auto nibble = hexValue(id[digit]);

        if (nibble < 0)
            return std::nullopt;

        value = value << 4 | static_cast<uint32_t>(nibble);
    }

    if (id[8] != '\0')
        return std::nullopt;

    return value;
}

DisplayDelegate::DisplayDelegate(const Parameter& paramToUse) noexcept
    : param(&paramToUse)
{
}

DisplayDelegate* DisplayDelegate::Clone() const
{
    return new DisplayDelegate(*this);
}

bool DisplayDelegate::ValueToString(float normalized, AAX_CString* text) const
{
    if (text == nullptr)
        return false;

    auto readout = param->valueToText(param->toPlain(normalized));
    text->Set(readout.c_str());
    return true;
}

bool DisplayDelegate::ValueToString(float normalized,
                                    int32_t maxChars,
                                    AAX_CString* text) const
{
    if (text == nullptr)
        return false;

    auto readout = param->valueToText(param->toPlain(normalized));

    if (maxChars >= 0 && readout.size() > static_cast<size_t>(maxChars))
        readout.resize(static_cast<size_t>(maxChars));

    text->Set(readout.c_str());
    return true;
}

bool DisplayDelegate::StringToValue(const AAX_CString& text, float* normalized) const
{
    if (normalized == nullptr)
        return false;

    auto plain = param->textToValue(text.StdString());

    if (!std::isfinite(plain))
    {
        *normalized = param->getNormalized();
        return true;
    }

    *normalized =
        param->toNormalized(std::clamp(plain, param->minValue(), param->maxValue()));
    return true;
}

AAX_IParameter* makeParameter(const ParameterList::Entry& entry)
{
    const auto& param = *entry.param;
    auto id = paramIdFor(entry.hostId);
    auto name = AAX_CString(entry.displayName.c_str());

    auto* result =
        new AAX_CParameter<float>(id.data(),
                                  name,
                                  param.toNormalized(param.defaultValue()),
                                  AAX_CLinearTaperDelegate<float>(0.f, 1.f),
                                  DisplayDelegate(param),
                                  entry.automatable);

    if (!param.shortName().empty())
        result->AddShortenedName(AAX_CString(param.shortName().c_str()));

    if (auto steps = param.numSteps(); steps > 0)
    {
        result->SetNumberOfSteps(static_cast<uint32_t>(steps) + 1);
        result->SetType(AAX_eParameterType_Discrete);
    }

    result->SetNormalizedValue(param.getNormalized());
    return result;
}

} // namespace MakeASound::AAX
