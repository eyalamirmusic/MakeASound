#pragma once

#include "VST3Common.h"
#include "../Core/Plugin.h"

#include "pluginterfaces/gui/iplugview.h"

namespace MakeASound::VST3
{

// A view on the plugin's editor (createEditor(), or GenericEditor when null),
// holding `owner` so the plugin outlives every view. Null where there is no UI.
Steinberg::IPlugView* createPlugView(FUnknown& owner, Plugin& plugin);

} // namespace MakeASound::VST3
