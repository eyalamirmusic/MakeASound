#pragma once

#include "Pluginval.h"

// One TU per platform: AUValidator-macOS.cpp runs auval, AUValidator-Default.cpp
// fails every .component.
namespace MakeASound::Pluginval
{
Result validateAudioUnit(const eacp::FilePath& bundle, const Options& options);
} // namespace MakeASound::Pluginval
