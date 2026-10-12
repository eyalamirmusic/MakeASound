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

    auto options = Web::WebEditorOptions {.category = "SynthPluginEditor",
                                          .storageName = "MakeASound Synth",
                                          .devServer = MAKEASOUND_WEB_DEV_SERVER,
                                          .initialSize = {600, 400},
                                          .constraints = {.resizable = true,
                                                          .minimum = {450, 300},
                                                          .maximum = {1200, 800},
                                                          .aspectRatio = {3, 2}}};

    return EA::makeOwned<Web::WebEditor>(plugin, std::move(options));
}

} // namespace MakeASoundExamples
