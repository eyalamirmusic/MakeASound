#pragma once

#include "FormatManager.h"

namespace MakeASound::Hosting
{

// Adds CLAP to `formats` where this build hosts it, and nothing where it does
// not: DefaultFormatsCLAP-On.cpp or -Off.cpp, chosen in CMake by whether the
// CLAP headers are there (MAKEASOUND_BUILD_CLAP, desktop).
void addCLAPFormat(FormatManager& formats);

} // namespace MakeASound::Hosting
