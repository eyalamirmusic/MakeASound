#pragma once

#include "ParameterApi.h"
#include "../Core/Editor.h"

#include <functional>
#include <string>

namespace MakeASound::Web
{

struct WebEditorOptions
{
    // The ResEmbed category the page was embedded under.
    std::string category;

    // Keys the WebView2 user-data folder on Windows, so two plugins in one host
    // never share one; unique per plugin.
    std::string storageName;

    // Loaded instead of the embedded page when reachable; empty never probes.
    std::string devServer;

    EditorSize initialSize;
    EditorConstraints constraints;
};

// An Editor showing a web page in eacp's WebView, bound to a ParameterApi over
// the plugin and to any further API handed to use(). The view is built on the
// first view(); values are polled at 30 Hz while attached, and a ResizeGrip is
// shown for a host without one. Message thread only.
class WebEditor : public Editor
{
public:
    WebEditor(Plugin& pluginToUse, WebEditorOptions optionsToUse);
    ~WebEditor() override;

    // The API must outlive the editor.
    template <typename Api>
    void use(Api& api)
    {
        addApi([&api](Miro::Bridge& bridge) { bridge.use(api); });
    }

    ParameterApi& parameterApi() noexcept { return parameters; }

    eacp::Graphics::View& view() override;

    EditorSize initialSize() const override { return options.initialSize; }
    EditorConstraints constraints() const override { return options.constraints; }

    void onAttached() override;
    void onRemoved() override;

private:
    struct Page;
    using Binder = std::function<void(Miro::Bridge&)>;

    void addApi(Binder binder);

    WebEditorOptions options;
    ParameterApi parameters;
    Vector<Binder> binders;
    OwningPointer<Page> page;
};

} // namespace MakeASound::Web
