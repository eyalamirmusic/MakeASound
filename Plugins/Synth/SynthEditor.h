#pragma once

#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace MakeASoundExamples
{

struct SynthPlugin;

// The React page where a web runtime exists, null elsewhere, so the host shows
// the generic editor instead.
MakeASound::OwningPointer<MakeASound::Editor> createSynthEditor(SynthPlugin& plugin);

} // namespace MakeASoundExamples
