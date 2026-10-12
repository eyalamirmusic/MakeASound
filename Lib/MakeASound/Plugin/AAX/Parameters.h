#pragma once

#include "AAXCommon.h"
#include "../Parameters/ParameterList.h"

#include <array>
#include <cstdint>
#include <optional>

namespace MakeASound::AAX
{

// AAX_CParamID: the 31-bit host id as eight lowercase hex digits, so an inserted or
// renamed parameter keeps everyone's automation.
using ParamId = std::array<char, 9>;

ParamId paramIdFor(uint32_t hostId) noexcept;

// Nullopt unless the id is exactly eight lowercase hex digits.
std::optional<uint32_t> hostIdOf(AAX_CParamID id) noexcept;

// Text both ways through the MakeASound parameter, in normalized units: the AAX
// parameter is 0..1 with a linear taper, so a skewed parameter reads right.
class DisplayDelegate : public AAX_IDisplayDelegate<float>
{
public:
    explicit DisplayDelegate(const Parameter& paramToUse) noexcept;

    DisplayDelegate* Clone() const override;
    bool ValueToString(float normalized, AAX_CString* text) const override;
    bool ValueToString(float normalized,
                       int32_t maxChars,
                       AAX_CString* text) const override;

    // Text that yields no number keeps the parameter's current value.
    bool StringToValue(const AAX_CString& text, float* normalized) const override;

private:
    const Parameter* param;
};

// One AAX_CParameter<float> for the entry, at the parameter's current value: steps
// and a discrete type from numSteps, automatable from the entry.
AAX_IParameter* makeParameter(const ParameterList::Entry& entry);

} // namespace MakeASound::AAX
