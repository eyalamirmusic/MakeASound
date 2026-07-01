#include "Types.h"

#include <eacp/Core/Threads/Timer.h>
#include <eacp/WebView/WebView.h>

using namespace eacp;
using namespace Graphics;

struct DemoApp
{
    DemoApp()
    {
        transport.getBridge().use(api);

        setApplicationMenuBar(buildDefaultWebViewMenuBar());
        window.setContentView(webView);
    }

    Api::DemoApi api;
    WebView webView {embeddedOptions("DemoWeb")};
    WebViewBridge transport {webView};
    Window window;
    Threads::Timer midiPollTimer {[this] { api.pollMidiPorts(); }, 2};
    Threads::Timer meterPollTimer {[this] { api.pollMeter(); }, 20};
};

int main()
{
    eacp::Apps::run<DemoApp>();
    return 0;
}
