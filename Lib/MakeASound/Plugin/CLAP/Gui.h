#pragma once

namespace MakeASound::CLAP
{

// clap.gui, over the Adapter behind the plugin, or null where there is no
// editor view, as there is none yet: NoGui.cpp is the only definition, and a
// Gui-<platform>.cpp chosen in CMakeLists.txt is where a view would go, as
// VST3's PlugView-<platform>.cpp. Asked only for a plugin whose hasEditor() is
// true.
const void* guiExtension() noexcept;

// Gives back the drawing devices the views brought up for the whole process, at
// the module's deinit while our code is still mapped. A no-op where there is no
// UI.
void releaseViewDevices();

} // namespace MakeASound::CLAP
