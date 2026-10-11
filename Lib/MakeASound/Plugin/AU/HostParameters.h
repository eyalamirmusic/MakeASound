#pragma once

#include "../Parameters/ParameterList.h"

#include <AudioToolbox/AudioToolbox.h>

#include <string>
#include <string_view>

namespace MakeASound::AU
{

// Every Copy/make below returns a +1 reference the caller releases.
CFStringRef makeCFString(std::string_view text);
std::string toUtf8(CFStringRef text);

// The host's view of one entry: its name, range and unit, and ValuesHaveStrings
// only where hasValueStrings answers.
void fillParameterInfo(const ParameterList::Entry& entry,
                       AudioUnitParameterInfo& info);

// A choice's names or a bool's two states, one per step; false for the rest.
bool hasValueStrings(const Parameter& param) noexcept;
CFArrayRef copyValueStrings(const Parameter& param);

CFStringRef copyTextForValue(const Parameter& param, float plain);

// Clamped to the range; the current value when the text means no number.
float valueForText(const Parameter& param, CFStringRef text);

} // namespace MakeASound::AU
