#pragma once

#include "VST3Common.h"
#include "../Core/Description.h"

#include "public.sdk/source/main/pluginfactory.h"

namespace MakeASound::VST3
{

// A new factory holding one reference, listing every plugin in `module`, which
// must outlive it: each class's create context is a pointer to its description.
Steinberg::CPluginFactory* makeFactory(const ModuleDescription& module);

// vst3ClassIdFor()'s four words (Core/Identity.h): MakeASound's
// FUID(manufacturer, plugin, 'VST3', 0), or a JUCE build's under a JUCE scheme.
// Never change it once shipped: a DAW finds a saved instance's plugin by it.
Steinberg::FUID classIdFor(const ModuleDescription& module,
                           const PluginDescription& plugin);

} // namespace MakeASound::VST3
