#include "WebEditor.h"
#include "../UI/ResizeGrip.h"

#include <eacp/Core/Core.h>
#include <eacp/WebView/WebView.h>

#include <optional>

namespace MakeASound::Web
{
using namespace eacp::Graphics;

namespace
{
WebView::Options webViewOptions(const WebEditorOptions& options)
{
    auto result = embeddedOptions(options.category);

    // A host's shortcuts and the standalone's typing keyboard keep working
    // while the page has focus.
    result.acceptFirstMouse = true;
    result.forwardUnhandledKeys = true;
    result.userDataFolderSuffix = options.storageName;
    result.embedded.preferDevServer = !options.devServer.empty();

    if (!options.devServer.empty())
        result.embedded.devServerURL = options.devServer;

    return result;
}
} // namespace

struct WebEditor::Page final : View
{
    explicit Page(const WebEditorOptions& options)
        : webView(webViewOptions(options))
    {
        addSubview(webView);
    }

    ~Page() override { hideGrip(); }

    void showGrip(Editor& editor)
    {
        grip.emplace(editor);
        addSubview(*grip);
        resized();
    }

    void hideGrip()
    {
        if (grip)
            removeSubview(*grip);

        grip.reset();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        webView.setBounds(area);

        if (grip)
            grip->setBounds(ResizeGrip::cornerOf(area));
    }

    WebView webView;
    WebViewBridge bridge {webView};
    std::optional<ResizeGrip> grip;
};

WebEditor::WebEditor(Plugin& pluginToUse, WebEditorOptions optionsToUse)
    : options(std::move(optionsToUse))
    , parameters(pluginToUse)
{
    use(parameters);
}

WebEditor::~WebEditor()
{
    polls.stop();
    parameters.endAllEdits();
}

void WebEditor::addApi(Binder binder)
{
    if (page != nullptr)
        binder(page->bridge.getBridge());

    binders.add(std::move(binder));
}

void WebEditor::addPoll(std::function<void()> poll, int hz)
{
    polls.add(std::move(poll), hz);
}

void WebEditor::poll()
{
    polls.pollAll();
}

void WebEditor::bind(Miro::Bridge& bridge)
{
    for (auto& binder: binders)
        binder(bridge);
}

View& WebEditor::view()
{
    if (page == nullptr)
    {
        page = EA::makeOwned<Page>(options);

        for (auto& binder: binders)
            binder(page->bridge.getBridge());
    }

    return *page;
}

void WebEditor::onAttached()
{
    view();
    polls.start();

    if (ResizeGrip::isWantedBy(*this))
        page->showGrip(*this);
}

void WebEditor::onRemoved()
{
    parameters.endAllEdits();
    polls.stop();

    if (page == nullptr)
        return;

    page->hideGrip();
}

} // namespace MakeASound::Web
