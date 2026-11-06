#pragma once

/** @file UTF-8 conversions to and from VST3 host strings. */

#include "VST3Common.h"

#include <string>
#include <string_view>

namespace MakeASound::VST3
{

/// UTF-8 into a host string buffer, truncated to 127 code units.
void copyTo(Vst::String128 destination, std::string_view utf8);

/// Empty for a null string or one that does not convert.
std::string toUtf8(const Vst::TChar* text);

} // namespace MakeASound::VST3
