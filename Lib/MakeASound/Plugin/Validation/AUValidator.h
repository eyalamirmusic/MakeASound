#pragma once

/** @file
    Runs Apple's auval on an Audio Unit .component. One source file per platform:
    AUValidator-macOS.cpp runs auval, AUValidator-Default.cpp fails every
    .component.
*/

#include "Pluginval.h"

namespace MakeASound::Pluginval
{
/// Blocks until auval finishes. Fails on every platform but macOS.
Result validateAudioUnit(const eacp::FilePath& bundle, const Options& options);
} // namespace MakeASound::Pluginval
