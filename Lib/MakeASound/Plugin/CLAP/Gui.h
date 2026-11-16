#pragma once

#include <memory>

namespace MakeASound::CLAP
{

// One instance's editor view, which clap.gui's create makes and its Adapter
// owns (GuiView.h, defined where the UI is built). Never made where it is not,
// so NoGui.cpp's deleter has nothing to delete.
class GuiView;

struct GuiViewDeleter
{
    void operator()(GuiView* view) const noexcept;
};

using GuiViewPointer = std::unique_ptr<GuiView, GuiViewDeleter>;

// clap.gui, over the Adapter behind the plugin, or null where there is no
// editor view: Gui.cpp and a Gui-<platform>.cpp where eacp graphics is built,
// as VST3's PlugView, and NoGui.cpp where it is not. Asked only for a plugin
// whose hasEditor() is true.
const void* guiExtension() noexcept;

// Gives back the drawing devices the views brought up for the whole process, at
// the module's deinit while our code is still mapped. A no-op where there is no
// UI.
void releaseViewDevices();

} // namespace MakeASound::CLAP
