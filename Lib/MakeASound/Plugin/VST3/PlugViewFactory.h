#pragma once

#include "VST3Common.h"
#include "../Core/Plugin.h"

#include "pluginterfaces/gui/iplugview.h"

namespace MakeASound::VST3
{

// A view on the plugin's editor (createEditor(), or GenericEditor when null),
// holding `owner` so the plugin outlives every view. Null where there is no UI.
// Asked only of a plugin whose hasEditor() is true; Adapter::createView answers
// null for the rest.
Steinberg::IPlugView* createPlugView(FUnknown& owner, Plugin& plugin);

// Gives back the drawing devices the views brought up for the whole process
// (eacp::Graphics::releaseSharedDevices), at the module's exit while our code
// is still mapped. A no-op where there is no UI.
void releaseViewDevices();

} // namespace MakeASound::VST3
