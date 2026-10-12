#include "SynthEditor.h"
#include "SynthPlugin.h"

#include <MakeASound/Plugin/Web/WebEditor.h>
#include <eacp/WebView/WebView.h>

namespace MakeASoundExamples
{

OwningPointer<Editor> createSynthEditor(SynthPlugin& plugin)
{
    // A Windows machine without the WebView2 runtime gets the generic editor
    // rather than a blank page.
    if (!eacp::Graphics::WebView::isRuntimeAvailable())
        return {};

    // Any shape, down to a phone's width: an iPhone in portrait is 375 to 440
    // points wide and much taller, which a ratio lock would leave mostly empty,
    // and the page reflows to whatever it gets.
    auto options = Web::WebEditorOptions {.category = "SynthPluginEditor",
                                          .storageName = "MakeASound Synth",
                                          .devServer = MAKEASOUND_WEB_DEV_SERVER,
                                          .initialSize = {600, 400},
                                          .constraints = {.resizable = true,
                                                          .minimum = {360, 240},
                                                          .maximum = {1200, 800}}};

    return EA::makeOwned<Web::WebEditor>(plugin, std::move(options));
}

} // namespace MakeASoundExamples
