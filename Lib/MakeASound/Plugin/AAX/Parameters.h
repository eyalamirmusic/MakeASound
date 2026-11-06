#pragma once

#include "AAXCommon.h"
#include "../Parameters/ParameterList.h"

#include <string_view>

namespace MakeASound::AAX
{

// An AAX_CParamID is the entry's aaxId (ParameterList, by AAXIdRule): under
// MakeASound's scheme the 31-bit host id as eight lowercase hex digits, so an
// inserted or renamed parameter keeps everyone's automation (under TamberPlug
// the same of its index id, Plug having shipped no AAX); under a JUCE scheme
// the id JUCE's AAX wrapper gave it, "MasterBypassID" for the bypass.

// The id the host knows the entry by: its aaxId cut to the first 31 bytes, as
// the SDK's AAX_CParameter keeps it (kAAX_ParameterIdentifierMaxSize less the
// NUL), whole characters or not. Only a non-ASCII JUCE id is ever cut, since
// ParameterList keeps every aaxId to 31 code points; a JUCE build's session
// names that parameter by the same 31 bytes.
std::string_view registeredId(std::string_view aaxId) noexcept;

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

// One AAX_CParameter<float> for the entry under its aaxId, at the parameter's
// current value: its steps and type by identity's StepRule (under NumSteps a
// stepped parameter is Discrete with numSteps() + 1 steps; under JuceDiscrete,
// Compat::juceAAXSteps), automatable from the entry.
AAX_IParameter* makeParameter(const ParameterList::Entry& entry,
                              const ParameterIdentity& identity);

} // namespace MakeASound::AAX
