#pragma once

#include "PagePolls.h"
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

// How the editor drives an API of the product's own, handed to use().
struct WebApiOptions
{
    // How often the editor calls the API's poll() while the page is attached;
    // 0 is the parameters' rate, PagePolls::defaultHz. An API without a poll()
    // is never called.
    int pollHz = 0;
};

// An Editor showing a web page in eacp's WebView, bound to a ParameterApi over
// the plugin and to any further API handed to use(). The view is built on the
// first view(); values are polled at 30 Hz while attached, and a ResizeGrip is
// shown for a host without one. Message thread only.
//
// A product adds channels of its own without touching this class: any Miro API
// (spectrum frames, a file picker, a preset browser) handed to use() is bound to
// the page as the parameters are.
//
// - Request/response: the API's commands. A command shaped
//   `void pick(const Req&, Miro::Completer<Res>)` answers later, e.g. when a
//   native file dialog closes.
// - Polling: an API with a `void poll()` is called at WebApiOptions::pollHz
//   while the page is attached, to publish whatever it gathered (a spectrum
//   read from an SPSC queue the audio thread fills, say) through its events.
//   addPoll() adds a call of any other kind.
// - Push: an API may publish its events on the message thread whenever it
//   likes; the page gets each at once. Work from another thread reaches the
//   page through poll(), or through the message thread.
class WebEditor : public Editor
{
public:
    WebEditor(Plugin& pluginToUse, WebEditorOptions optionsToUse);
    ~WebEditor() override;

    // The API must outlive the editor.
    template <typename Api>
    void use(Api& api, WebApiOptions apiOptions = {})
    {
        addApi([&api](Miro::Bridge& bridge) { bridge.use(api); });

        if constexpr (requires { api.poll(); })
            addPoll([&api] { api.poll(); }, apiOptions.pollHz);
    }

    // A call of the product's own, made `hz` times a second (0 is
    // PagePolls::defaultHz) while the page is attached.
    void addPoll(std::function<void()> poll, int hz = 0);

    // Makes every poll once, as the timers do while the page is attached.
    void poll();

    // Binds every API handed to use() to `bridge` too, as the page's own is:
    // for a second transport (a remote control page), or a test.
    void bind(Miro::Bridge& bridge);

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

    // After the APIs its calls name, so it stops first.
    PagePolls polls;
};

} // namespace MakeASound::Web
