#include "Types.h"

#include <eacp/Core/Threads/Timer.h>
#include <eacp/WebView/WebView.h>

using namespace eacp;
using namespace Graphics;

struct SynthApp
{
    SynthApp()
    {
        transport.getBridge().use(api);

        setApplicationMenuBar(buildDefaultWebViewMenuBar());
        window.setContentView(webView);
    }

    Api::SynthApi api;
    WebView webView {embeddedOptions("SynthWeb")};
    WebViewBridge transport {webView};
    Window window;
    Threads::Timer midiPollTimer {[this] { api.pollMidiPorts(); }, 2};
};

int main()
{
    eacp::Apps::run<SynthApp>();
    return 0;
}
